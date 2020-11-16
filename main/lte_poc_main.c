/* PPPoS Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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

#include "lte_poc.h"

#define BROKER_URL "mqtt://mqtt.eclipse.org"

static const char *TAG = "LTE_POC";
static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int STOP_BIT = BIT1;
static const int GOT_DATA_BIT = BIT2;

static void modem_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ESP_MODEM_EVENT_PPP_START:
        ESP_LOGI(TAG, "Modem PPP Started");
        break;
    case ESP_MODEM_EVENT_PPP_STOP:
        ESP_LOGI(TAG, "Modem PPP Stopped");
        xEventGroupSetBits(event_group, STOP_BIT);
        break;
    case ESP_MODEM_EVENT_UNKNOWN:
        ESP_LOGW(TAG, "Unknow line received: %s", (char *)event_data);
        break;
    default:
        break;
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/esp-pppos", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
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
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        xEventGroupSetBits(event_group, GOT_DATA_BIT);
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

static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %d", event_id);
    if (event_id == NETIF_PPP_ERRORUSER) {
        /* User interrupted event from esp-netif */
        esp_netif_t *netif = event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", netif);
    }
}


static void on_ip_event(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %d", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}

void app_main(void)
{
#if CONFIG_EXAMPLE_DELAY_LTE_INIT
    ESP_LOGI(TAG, "wait for 30 seconds as a work-around for crashes");
    vTaskDelay(30000 / portTICK_PERIOD_MS);
#endif

    lte_uart_start();
    lte_register_network();
    LteInfo lte_info = lte_get_info();
    ESP_LOGI(TAG, "LTE quality: %d", lte_info.signal_quality);
    lte_stop_listen();

#if CONFIG_LWIP_PPP_PAP_SUPPORT
    esp_netif_auth_type_t auth_type = NETIF_PPP_AUTHTYPE_PAP;
#elif CONFIG_LWIP_PPP_CHAP_SUPPORT
    esp_netif_auth_type_t auth_type = NETIF_PPP_AUTHTYPE_CHAP;
#else
#error "Unsupported AUTH Negotiation"
#endif
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    event_group = xEventGroupCreate();

    // Init netif object
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&cfg);
    assert(esp_netif);

    /* create dte object */
    esp_modem_dte_config_t config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    modem_dte_t *dte = esp_modem_dte_init(&config);
    /* Register event handler */
    ESP_ERROR_CHECK(esp_modem_set_event_handler(dte, modem_event_handler, ESP_EVENT_ANY_ID, NULL));
    /* create dce object */
#if CONFIG_EXAMPLE_MODEM_DEVICE_SIM800
    modem_dce_t *dce = sim800_init(dte);
#elif CONFIG_EXAMPLE_MODEM_DEVICE_BG96
    modem_dce_t *dce = bg96_init(dte);
#else
#error "Unsupported DCE"
#endif
    ESP_ERROR_CHECK(dce->set_flow_ctrl(dce, MODEM_FLOW_CONTROL_NONE));
    ESP_ERROR_CHECK(dce->store_profile(dce));
    /* Print Module ID, Operator, IMEI, IMSI */
    ESP_LOGI(TAG, "Module: %s", dce->name);
    ESP_LOGI(TAG, "Operator: %s", dce->oper);
    ESP_LOGI(TAG, "IMEI: %s", dce->imei);
    ESP_LOGI(TAG, "IMSI: %s", dce->imsi);
    /* Get signal quality */
    uint32_t rssi = 0, ber = 0;
    ESP_ERROR_CHECK(dce->get_signal_quality(dce, &rssi, &ber));
    ESP_LOGI(TAG, "rssi: %d, ber: %d", rssi, ber);
    /* Get battery voltage */
    uint32_t voltage = 0, bcs = 0, bcl = 0;
    ESP_ERROR_CHECK(dce->get_battery_status(dce, &bcs, &bcl, &voltage));
    ESP_LOGI(TAG, "Battery voltage: %d mV", voltage);
    /* setup PPPoS network parameters */
    esp_netif_ppp_set_auth(esp_netif, auth_type, CONFIG_EXAMPLE_MODEM_PPP_AUTH_USERNAME, CONFIG_EXAMPLE_MODEM_PPP_AUTH_PASSWORD);
    void *modem_netif_adapter = esp_modem_netif_setup(dte);
    esp_modem_netif_set_default_handlers(modem_netif_adapter, esp_netif);
    /* attach the modem to the network interface */
    esp_netif_attach(esp_netif, modem_netif_adapter);
    /* Wait for IP address */
    xEventGroupWaitBits(event_group, CONNECT_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    /* Config MQTT */
    esp_mqtt_client_config_t mqtt_config = {
        .uri = BROKER_URL,
        .event_handle = mqtt_event_handler,
    };
    esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_start(mqtt_client);
    xEventGroupWaitBits(event_group, GOT_DATA_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    esp_mqtt_client_destroy(mqtt_client);

    int test_count = 0;
    int success_count = 0;
    
    while (test_count < CONFIG_EXAMPLE_NUM_TEST_MESSAGES) {
        if (run_http_test() == ESP_OK) {
            success_count += 1;
        }
        test_count += 1;

        ESP_LOGI(TAG, "success rate: %d/%d", success_count, test_count);

        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }

    /* Exit PPP mode */
    ESP_ERROR_CHECK(esp_modem_stop_ppp(dte));
    /* Destroy the netif adapter withe events, which internally frees also the esp-netif instance */
    esp_modem_netif_clear_default_handlers(modem_netif_adapter);
    esp_modem_netif_teardown(modem_netif_adapter);
    xEventGroupWaitBits(event_group, STOP_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
    /* Power down module */
    ESP_ERROR_CHECK(dce->power_down(dce));
    ESP_LOGI(TAG, "Power down");
    ESP_ERROR_CHECK(dce->deinit(dce));
    ESP_ERROR_CHECK(dte->deinit(dte));
}
