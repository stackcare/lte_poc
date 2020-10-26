//
//  Copyright © 2020 Stack Care Inc. All rights reserved.
//

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "iotc.h"
#include "iotc_jwt.h"
#include "esp_system.h"
#include "esp_event.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

#include "lte_poc.h"

#define BUF_SIZE (1024)
#define JWT_EXPIRATION_PERIOD 86400 // 24 hours. This is the maximum allowed according to doc

#define IOTC_UNUSED(x) (void)(x)
#define DEVICE_PATH "projects/%s/locations/%s/registries/%s/devices/%s"
#define SUBSCRIBE_TOPIC_COMMAND "/devices/%s/commands/#"
#define SUBSCRIBE_TOPIC_COMMAND_NO_SUFFIX "/devices/%s/commands"
#define SUBSCRIBE_TOPIC_CONFIG "/devices/%s/config"
#define TOPIC_MQTT_MOTION "/devices/%s/events/mqtt-motion"
#define TOPIC_MQTT_HEARTBEAT "/devices/%s/events/mqtt-heartbeat"
#define PUBLISH_TOPIC_STATE "/devices/%s/state"

#define CONNECTION_TIMEOUT 60 //seconds
#define KEEPALIVE_TIMEOUT 180 //seconds

#define OFFLINE_THRESHOLD 10
#define OFFLINE_REBOOT_WAIT (30 * 60) // 30 minutes

typedef enum {
    SS_MQTT_UNKNOWN,
    SS_MQTT_NOT_ACTIVATED,
    SS_MQTT_CONNECTING,
    SS_MQTT_DISCONNECTED,
    SS_MQTT_DISCONNECTED_RETRYING,
    SS_MQTT_CONNECTED
} ss_mqtt_state_t;

static const char *TAG = "Mqtt";

static ss_mqtt_state_t s_mqtt_state = SS_MQTT_NOT_ACTIVATED;

static TaskHandle_t s_control_task = NULL;
static bool s_mqtt_running = false;
static bool s_is_connected = false;
static bool s_is_offline = false;
static time_t s_offline_time;
static char *s_dev_id_list[MAX_NUM_SENSORS];
static int s_dev_num;

static HubInfo s_hub_info;
static char *s_priv_key = NULL;
static char *s_jwt_token = NULL;
static int s_publish_count = 0;
static int s_publish_confirmed = 0;
static char *s_topic_command;
static char *s_topic_command_no_suffix;
static char *s_topic_config;

static iotc_context_handle_t s_iotc_context = IOTC_INVALID_CONTEXT_HANDLE;
static iotc_mqtt_qos_t s_iotc_qos = IOTC_MQTT_QOS_AT_LEAST_ONCE;

static void mqtt_task();
static void iotc_mqttlogic_subscribe_callback(iotc_context_handle_t in_context_handle, iotc_sub_call_type_t call_type, const iotc_sub_call_params_t * const params, iotc_state_t state, void *user_data);
//static void on_connection_state_changed(iotc_context_handle_t in_context_handle, void *data, iotc_state_t state);

static char *mqtt_state_name(ss_mqtt_state_t state)
{
    switch (state) {
    case SS_MQTT_UNKNOWN:
        return "SS_MQTT_UNKNOWN";
    case SS_MQTT_NOT_ACTIVATED:
        return "SS_MQTT_NOT_ACTIVATED";
    case SS_MQTT_CONNECTING:
        return "SS_MQTT_CONNECTING";
    case SS_MQTT_DISCONNECTED:
        return "SS_MQTT_DISCONNECTED";
    case SS_MQTT_DISCONNECTED_RETRYING:
        return "SS_MQTT_DISCONNECTED_RETRYING";
    case SS_MQTT_CONNECTED:
        return "SS_MQTT_CONNECTED";
    default:
        return "Invalid";
    }
}

static ss_mqtt_state_t ss_get_mqtt_state()
{
    ss_mqtt_state_t value = s_mqtt_state;
    return value;
}

static void ss_set_mqtt_state(ss_mqtt_state_t value)
{
    ss_mqtt_state_t old = s_mqtt_state;
    s_mqtt_state = value;
    ESP_LOGI(TAG, "mqtt state changed. %s --> %s", mqtt_state_name(old), mqtt_state_name(value));
}

static esp_err_t s_create_jwt()
{

    /* Generate the client authentication JWT, which will serve as the MQTT
     * password. */
    char *jwt = (char *) malloc(IOTC_JWT_SIZE);
    size_t bytes_written = 0;

    /* Format the key type descriptors so the client understands
     which type of key is being represented. In this case, a PEM encoded
     byte array of a ES256 key. */
    iotc_crypto_key_data_t iotc_connect_private_key_data;
    iotc_connect_private_key_data.crypto_key_signature_algorithm = IOTC_CRYPTO_KEY_SIGNATURE_ALGORITHM_ES256;
    iotc_connect_private_key_data.crypto_key_union_type = IOTC_CRYPTO_KEY_UNION_TYPE_PEM;
    iotc_connect_private_key_data.crypto_key_union.key_pem.key = s_priv_key;

    iotc_state_t state = iotc_create_iotcore_jwt(s_hub_info.projectId,
                                                 JWT_EXPIRATION_PERIOD,
                                                 &iotc_connect_private_key_data,
                                                 jwt,
                                                 IOTC_JWT_SIZE,
                                                 &bytes_written);
    if (IOTC_STATE_OK != state) {
        ESP_LOGE(TAG, "iotc_create_iotcore_jwt returned with error: %ul", state);
        free(jwt);
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "JWT created: %s", jwt);
    }

    if (s_jwt_token != NULL) {
        free(s_jwt_token);
        s_jwt_token = NULL;
    }

    s_jwt_token = jwt;

    return ESP_OK;
}

esp_err_t mqtt_init_iotc()
{
    if (s_iotc_context != IOTC_INVALID_CONTEXT_HANDLE) {
        ESP_LOGW(TAG, "IOTC is already initialized");
        return ESP_OK;
    }

    strcpy(s_hub_info.hubId, HUB_ID);
    strcpy(s_hub_info.projectId, PROJECT_ID);
    strcpy(s_hub_info.locale, IOTC_LOCALE);
    strcpy(s_hub_info.registryId, IOTC_REGISTRY_ID);

    asprintf(&s_priv_key, "%s", PRIV_KEY);

    /* initialize iotc library and create a context to use to connect to the
     * GCP IoT Core Service. */
    const iotc_state_t error_init = iotc_initialize();
    if (IOTC_STATE_OK != error_init) {
        printf(" iotc failed to initialize, error: %d\n", error_init);
        return ESP_FAIL;
    }

    /*  Create a connection context. A context represents a Connection
     on a single socket, and can be used to publish and subscribe
     to numerous topics. */
    s_iotc_context = iotc_create_context();
    if (IOTC_INVALID_CONTEXT_HANDLE >= s_iotc_context) {
        ESP_LOGI(TAG, " iotc failed to create context, error: %d\n", s_iotc_context);
        return ESP_FAIL;
    }

    return s_create_jwt();
}

const char *mqtt_current_jwt()
{
    return s_jwt_token;
}

static void check_offline()
{
    bool seems_offline = (s_publish_count - s_publish_confirmed) > OFFLINE_THRESHOLD;
    if (s_is_offline != seems_offline) {
        s_is_offline = seems_offline;
        if (s_is_offline) {
            time(&s_offline_time);
            //zb_set_led(YELLOW, ON);
            ESP_LOGW(TAG, "MQTT gone offline");
        } else {
            //zb_set_led(YELLOW, OFF);
            ESP_LOGI(TAG, "MQTT is back online");
        }
    }
}

static void on_publish(iotc_context_handle_t in_context_handle, void* data, iotc_state_t state)
{
    int pid = (int) data;
    ESP_LOGI(TAG, "publishing completed for id: %d, state: %d", pid, state);
    if (s_publish_confirmed < pid) {
        s_publish_confirmed = pid;
    }

    check_offline();

    //turn_red_led_off_if_on();
}

static esp_err_t s_mqtt_publish(const char *topic, const uint8_t *msg, size_t len)
{
    if (s_is_connected == false) {
        ESP_LOGI(TAG, "s_is_connected : FALSE");
        return ESP_FAIL;
    }

    int result;
    if (len <= 0) {
        result = iotc_publish(s_iotc_context,
                              topic,
                              (const char *)msg,
                              s_iotc_qos,
                              on_publish,
                              (void*) s_publish_count);
    } else {
        result = iotc_publish_data(s_iotc_context,
                                   topic,
                                   msg,
                                   len,
                                   s_iotc_qos,
                                   on_publish,
                                   (void*) s_publish_count);
    }
    ESP_LOGI(TAG, "publish request id: %d, state: %d", s_publish_count, result);
    s_publish_count += 1;

    check_offline();

    return ESP_OK;
}

esp_err_t mqtt_publish(const char *topic, const char* msg)
{
    return s_mqtt_publish(topic, (const uint8_t *)msg, 0);
}

esp_err_t mqtt_publish_data(const char* topic, const uint8_t* msg, size_t len)
{
    return s_mqtt_publish(topic, msg, len);
}

static void run_task(void* param)
{
    s_mqtt_running = true;
    mqtt_task();
    s_mqtt_running = false;
}

void mqtt_start() {
    if (s_mqtt_running) {
        ESP_LOGW(TAG, "MQTT is already running");
        return;
    }

    s_dev_id_list[0] = DEVICE_ID;
    s_dev_num = 1;

    s_control_task = xTaskGetCurrentTaskHandle();
    xTaskCreate(&run_task, "mqtt_task", BUF_SIZE * 8, NULL, DEFAULT_PRIORITY, NULL);

    uint32_t notify_value = 0;
    xTaskNotifyWait(ULONG_MAX, ULONG_MAX, &notify_value, portMAX_DELAY);
    s_control_task = NULL;
}

void mqtt_stop()
{
    if (!s_mqtt_running) {
        ESP_LOGW(TAG, "MQTT is NOT running");
        return;
    }

    s_control_task = xTaskGetCurrentTaskHandle();
    iotc_shutdown_connection(s_iotc_context);
    uint32_t notify_value = 0;
    xTaskNotifyWait(ULONG_MAX, ULONG_MAX, &notify_value, portMAX_DELAY);
    s_control_task = NULL;
}

void mqtt_attach_device(const char* device_id)
{
    char* attach_topic = NULL;
    asprintf(&attach_topic, "/devices/%s/attach", device_id);
    ESP_LOGI(TAG, "publishing device attachment to %s", attach_topic);
    mqtt_publish(attach_topic, "{}");
    free(attach_topic);
}

// TODO: delegate some tasks to Control Center for better thread safety
static void on_connection_state_changed(iotc_context_handle_t in_context_handle, void *data, iotc_state_t state) {

    iotc_connection_data_t *conn_data = (iotc_connection_data_t *) data;
    bool try_reconnect = false;

    switch (conn_data->connection_state) {
    /* IOTC_CONNECTION_STATE_OPENED means that the connection has been
     established and the IoTC Client is ready to send/recv messages */
    case IOTC_CONNECTION_STATE_OPENED:
        ESP_LOGI(TAG, "IOTC_CONNECTION_STATE_OPENED");
        ESP_LOGI(TAG, "Connected!");
        ss_set_mqtt_state(SS_MQTT_CONNECTED);
         
        for (int i = 0; i < s_dev_num; i++) {
            mqtt_attach_device(s_dev_id_list[i]);
        }

        iotc_subscribe(in_context_handle, s_topic_command, IOTC_MQTT_QOS_AT_LEAST_ONCE,
                &iotc_mqttlogic_subscribe_callback, /*user_data=*/NULL);

        iotc_subscribe(in_context_handle, s_topic_config, IOTC_MQTT_QOS_AT_LEAST_ONCE,
                &iotc_mqttlogic_subscribe_callback, /*user_data=*/NULL);


        //event_post(MQTT_EVENTS, EVENT_MQTT_CONNECTED, NULL, 0);

        break;

    /* IOTC_CONNECTION_STATE_OPEN_FAILED is set when there was a problem
        when establishing a connection to the server. The reason for the error
        is contained in the 'state' variable. Here we log the error state and
        exit out of the application. */
    case IOTC_CONNECTION_STATE_OPEN_FAILED:
        ESP_LOGI(TAG, "IOTC_CONNECTION_STATE_OPEN_FAILED");
        ESP_LOGW(TAG, "ERROR!\tConnection has failed. Reason %d", state);
        ss_set_mqtt_state(SS_MQTT_DISCONNECTED_RETRYING);

        try_reconnect = true;
        break;

    /* IOTC_CONNECTION_STATE_CLOSED is set when the IoTC Client has been
        disconnected. The disconnection may have been caused by some external
        issue, or user may have requested a disconnection. In order to
        distinguish between those two situation it is advised to check the state
        variable value. If the state == IOTC_STATE_OK then the application has
        requested a disconnection via 'iotc_shutdown_connection'. If the state !=
        IOTC_STATE_OK then the connection has been closed from one side. */
    case IOTC_CONNECTION_STATE_CLOSED:
        ESP_LOGW(TAG, "IOTC_CONNECTION_STATE_CLOSED. reason: %d", state);

        if (state == IOTC_STATE_OK) {
            iotc_events_stop();
            ss_set_mqtt_state(SS_MQTT_DISCONNECTED);
        } else {
            try_reconnect = true;
            ss_set_mqtt_state(SS_MQTT_DISCONNECTED_RETRYING);
        }
        break;

    default:
        ESP_LOGI(TAG, "Unknown Mqtt connection state.");
        break;
    }

    if (try_reconnect) {
        ESP_LOGI(TAG, "attempting to reconnect to MQTT...");
        // renew the JWT token first in case the cause of disconnect is JWT expiration
        if (s_create_jwt() == ESP_OK) {
            //event_post(MQTT_EVENTS, EVENT_MQTT_JWT_RENEWED, s_jwt_token, strlen(s_jwt_token) + 1);
            iotc_connect(in_context_handle, conn_data->username, s_jwt_token,
                         conn_data->client_id, conn_data->connection_timeout,
                         conn_data->keepalive_timeout, &on_connection_state_changed);
        }
    }
}

static void iotc_mqttlogic_subscribe_callback(iotc_context_handle_t in_context_handle, iotc_sub_call_type_t call_type,
        const iotc_sub_call_params_t * const params, iotc_state_t state, void *user_data) {
    IOTC_UNUSED(in_context_handle);
    IOTC_UNUSED(call_type);
    IOTC_UNUSED(state);
    IOTC_UNUSED(user_data);
    if (params != NULL && params->message.topic != NULL) {
        ESP_LOGI(TAG, "Subscription Topic: %s", params->message.topic);
        char *sub_message = (char *) malloc(params->message.temporary_payload_data_length + 1);
        if (sub_message == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory.");
            return;
        }
        memcpy(sub_message, params->message.temporary_payload_data, params->message.temporary_payload_data_length);
        sub_message[params->message.temporary_payload_data_length] = '\0';
        ESP_LOGI(TAG, "Message Payload: %s ", sub_message);

        if (strcmp(s_topic_command, params->message.topic) == 0) {
            //event_post(MQTT_EVENTS, EVENT_MQTT_COMMAND_RECEIVED, sub_message, strlen(sub_message) + 1);
            ESP_LOGI(TAG, "MQTT command received");
        } else if (strcmp(s_topic_command_no_suffix, params->message.topic) == 0) {
            // Note: library incompatibility found.
            // With the google-iotc library upgraded to 1.0.2,
            // it's noticed that params->message.topic value
            // doesn't have "/#" suffix at the end.
            // Have to handle the issue here.
            //event_post(MQTT_EVENTS, EVENT_MQTT_COMMAND_RECEIVED, sub_message, strlen(sub_message) + 1);
            ESP_LOGI(TAG, "MQTT command received");
        } else if (strcmp(s_topic_config, params->message.topic) == 0) {
            //event_post(MQTT_EVENTS, EVENT_MQTT_CONFIG_RECEIVED, sub_message, strlen(sub_message) + 1);
            ESP_LOGI(TAG, "MQTT config received");
		}
		free(sub_message);
    }
}

static void mqtt_task() {

    /*  Queue a connection request to be completed asynchronously.
     The 'on_connection_state_changed' parameter is the name of the
     callback function after the connection request completes, and its
     implementation should handle both successful connections and
     unsuccessful connections as well as disconnections. */
    const uint16_t connection_timeout = CONNECTION_TIMEOUT;
    const uint16_t keepalive_timeout  = KEEPALIVE_TIMEOUT;


    ESP_LOGI(TAG,"HUB ID TO CONNECT TO : %s", s_hub_info.hubId);

    asprintf(&s_topic_command, SUBSCRIBE_TOPIC_COMMAND, s_hub_info.hubId);
    ESP_LOGI(TAG, "command topic constructed: %s", s_topic_command);
    asprintf(&s_topic_command_no_suffix, SUBSCRIBE_TOPIC_COMMAND_NO_SUFFIX, s_hub_info.hubId);
    ESP_LOGD(TAG, "command topic name without suffix also created");

    asprintf(&s_topic_config, SUBSCRIBE_TOPIC_CONFIG, s_hub_info.hubId);
    ESP_LOGI(TAG, "config topic constructed: %s", s_topic_config);

    char *device_path = NULL;
    asprintf(&device_path, DEVICE_PATH, s_hub_info.projectId, s_hub_info.locale, s_hub_info.registryId,
            s_hub_info.hubId);
    if (device_path == NULL) {
        ESP_LOGE(TAG, "failed to create device_path");
        goto end;
    }
    ESP_LOGI(TAG, "device_path: %s", device_path);
    ESP_LOGI(TAG, "JWT token: %s", s_jwt_token);
    int res = iotc_connect(s_iotc_context, NULL, s_jwt_token, device_path, connection_timeout, keepalive_timeout,
            &on_connection_state_changed);
    free(device_path);
    device_path = NULL;

    if (res == IOTC_STATE_OK) {
        ESP_LOGI(TAG, "connection requested");
        if (s_control_task != NULL) {
            xTaskNotify(s_control_task, 1, eSetValueWithOverwrite);
        }
    } else {
        ESP_LOGE(TAG, "connection request failed");
        goto end;
    }
    s_is_connected = true;
    
    /* The IoTC Client was designed to be able to run on single threaded devices.
     As such it does not have its own event loop thread. Instead you must
     regularly call the function iotc_events_process_blocking() to process
     connection requests, and for the client to regularly check the sockets for
     incoming data. This implementation has the loop operate endlessly. The loop
     will stop after closing the connection, using iotc_shutdown_connection() as
     defined in on_connection_state_change logic, and exit the event handler
     handler by calling iotc_events_stop(); */
    iotc_events_process_blocking();

    iotc_delete_context(s_iotc_context);
    s_iotc_context = IOTC_INVALID_CONTEXT_HANDLE;
    s_is_connected = false;

    ESP_LOGI(TAG, "Shutting down MQTT Task.");
    iotc_shutdown();

    if (s_control_task != NULL) {
        xTaskNotify(s_control_task, 1, eSetValueWithOverwrite);
    }

 end:
    
    vTaskDelete(NULL);
}
