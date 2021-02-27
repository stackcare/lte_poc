#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "mqtt_client.h"
#include "esp_modem.h"
#include "esp_modem_netif.h"
#include "esp_log.h"
#include "sim800.h"
#include "bg96.h"

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"

#include "include/pppos_client_main.h"
#include "include/esp32_http_client.h"
#include "include/esp32_mqtt.h"
#include "include/esp32_ota.h"

static const char *TAG = "mqtt";

esp_mqtt_client_handle_t g_mqtt_client;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
         msg_id = esp_mqtt_client_subscribe(g_mqtt_client, "/topic/esp-pppos", 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", "esp32-pppos", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        //printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("\nmqtt data=%.*s\r\n", event->data_len, event->data);
        //xEventGroupSetBits(event_group, GOT_DATA_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "MQTT other event id: %d", event->event_id);
        break;
    }
    return ESP_OK;
}

void esp32_mqtt_start(void)
{
    const esp_mqtt_client_config_t mqtt_config = {
            .host = "106.14.251.166",
            .port = 1883,
            .lwt_topic = "SERVER_TOPIC",
            .lwt_msg = "i guale !!!",
            .lwt_qos = 2,
            .lwt_msg_len = sizeof("i guale !!!"),
            .event_handle = mqtt_event_handler,
            .username = "chb",
            .password = "chb123456"
        };

    g_mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_start(g_mqtt_client);
}

int mqtt_subscribe(char * data, size_t len)
{
    int msg_id;
    if(g_mqtt_client != NULL){
         msg_id = esp_mqtt_client_publish(g_mqtt_client, "/topic/esp-pppos", data, len, 0, 0);
         return 0;
    }
    return -1;
}