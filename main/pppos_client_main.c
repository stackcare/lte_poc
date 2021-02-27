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

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"

#include "include/pppos_client_main.h"
#include "include/esp32_http_client.h"
#include "include/esp32_mqtt.h"
#include "include/esp32_ota.h"
#include "include/database.h"
#include "driver/gpio.h"

#define BROKER_URL "mqtt://mqtt.eclipse.org"

static const char *TAG = "pppos";
EventGroupHandle_t event_group = NULL;
const int CONNECT_BIT = BIT0;
const int STOP_BIT = BIT1;
const int GOT_DATA_BIT = BIT2;
//static const int CONNECT_LTE_STATION_BIT = BIT3;


#define TWDT_TIMEOUT_S          3
modem_dte_t *g_dte = NULL;
modem_dce_t *g_dce = NULL;

void *g_modem_netif_adapter = NULL;
esp_netif_t *g_ppp_esp_netif = NULL;

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
        //ESP_LOGW(TAG, "Unknow line received: %s", (char *)event_data);
        break;
    default:
        break;
    }
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
        ota_http_cleanup();
        ota_task_fatal_error();
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}


static void ppp_task(void *pvParameter)
{
	gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.
    io_conf.pin_bit_mask = (1ULL<<16);
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 1;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
	//vTaskDelay(1000/portTICK_PERIOD_MS);
	gpio_set_level(16, 0);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	gpio_set_level(16, 1);
    pppos_main();
}

void pppos_main_task_init()
{
    xTaskCreatePinnedToCore(&ppp_task, "ota_task", 1024*8, NULL, 10, NULL,1);
}


void pppos_main(void)
{
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

    

    // Init netif object
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
    g_ppp_esp_netif = esp_netif_new(&cfg);
    assert(g_ppp_esp_netif);

    /* create dte object */
    esp_modem_dte_config_t config;// = ESP_MODEM_DTE_DEFAULT_CONFIG();
                                             
    config.port_num = UART_NUM_1;
    config.data_bits = UART_DATA_8_BITS;
    config.stop_bits = UART_STOP_BITS_1;
    config.parity = UART_PARITY_DISABLE;
    config.baud_rate = 115200;
    config.flow_control = MODEM_FLOW_CONTROL_NONE;
    
    //config.baud_rate = db_get_lte_uart_rate();
    g_dte = esp_modem_dte_init(&config);
    /* Register event handler */
    ESP_ERROR_CHECK(esp_modem_set_event_handler(g_dte, modem_event_handler, ESP_EVENT_ANY_ID, NULL));

    g_dce = bg96_init(g_dte);
    if(g_dce == NULL){
        g_dce->deinit(g_dce);
    }else{
        bg96_ppp_start();
        ESP_ERROR_CHECK(g_dce->set_flow_ctrl(g_dce, MODEM_FLOW_CONTROL_NONE));
        ESP_ERROR_CHECK(g_dce->store_profile(g_dce));
        /* Print Module ID, Operator, IMEI, IMSI */
        ESP_LOGI(TAG, "Module: %s", g_dce->name);
        ESP_LOGI(TAG, "Operator: %s", g_dce->oper);
        ESP_LOGI(TAG, "IMEI: %s", g_dce->imei);
        ESP_LOGI(TAG, "IMSI: %s", g_dce->imsi);
    
        /* setup PPPoS network parameters */
        esp_netif_ppp_set_auth(g_ppp_esp_netif, auth_type, CONFIG_EXAMPLE_MODEM_PPP_AUTH_USERNAME, CONFIG_EXAMPLE_MODEM_PPP_AUTH_PASSWORD);
        g_modem_netif_adapter = esp_modem_netif_setup(g_dte);
        esp_modem_netif_set_default_handlers(g_modem_netif_adapter, g_ppp_esp_netif);
        /* attach the modem to the network interface */
        esp_netif_attach(g_ppp_esp_netif, g_modem_netif_adapter);

    }
    while(1){
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}


void pppos_uninit(void)
{
    ESP_ERROR_CHECK(esp_modem_stop_ppp(g_dte));
        /* Destroy the netif adapter withe events, which internally frees also the esp-netif instance */
    esp_modem_netif_clear_default_handlers(g_modem_netif_adapter);
    esp_modem_netif_teardown(g_modem_netif_adapter);

    /* Power down module */
    ESP_ERROR_CHECK(g_dce->power_down(g_dce));
    ESP_LOGI(TAG, "Power down");
    ESP_ERROR_CHECK(g_dce->deinit(g_dce));
    ESP_ERROR_CHECK(g_dte->deinit(g_dte));

    esp_netif_destroy(g_ppp_esp_netif);
}

void get_lte_lqi(char * rsrp, char * rsrq, char * rssi)
{
    do{
        vTaskDelay(1000/portTICK_PERIOD_MS);
        g_dte->change_mode(g_dte,MODEM_COMMAND_MODE);
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }while(g_dce->mode != MODEM_COMMAND_MODE );
    vTaskDelay(1000/portTICK_PERIOD_MS);
    bg96_modem_dce_t *bg96_dce = __containerof(g_dce, bg96_modem_dce_t, parent);
    bg96_get_servingcell(bg96_dce);
    vTaskDelay(1000/portTICK_PERIOD_MS);

    if(rsrp && rsrq && rssi){
        memcpy(rsrp,g_dce->servingcell.rsrp,sizeof(g_dce->servingcell.rsrp));
        memcpy(rsrq,g_dce->servingcell.rsrq,sizeof(g_dce->servingcell.rsrq));
        memcpy(rssi,g_dce->servingcell.rssi,sizeof(g_dce->servingcell.rssi));
    }
    do{
        bg96_dce->parent.switch_pppMode(bg96_dce);
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }while(g_dce->mode != MODEM_PPP_MODE );
}

void get_sim_iccid(char * iccid)
{
    if(iccid){
        memcpy(iccid,g_dce->iccid,19);
    }
}
unsigned char get_ppp_mode(void)
{
    return g_dce->mode;
}