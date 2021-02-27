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
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
static const char *TAG = "user";

static void uasr_task(void *pvParameter)
{
    static unsigned int times = 0;
    char rsrp[5] = {0},rsrq[5] = {0},rssi[5] = {0};
    char iccid[20] = {0};
    while(1){
         /* Wait for IP address */
        xEventGroupWaitBits(event_group, CONNECT_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        esp32_mqtt_start();
        
        get_sim_iccid(iccid);
        printf("iccid  %s\n",iccid);
        while(1){
            if(times < 60){
                if(MODEM_PPP_MODE == get_ppp_mode()){
                    http_rest_with_url();
                    mqtt_subscribe("this is test data \n", sizeof("this is test data \n"));
                }
                vTaskDelay(1000/portTICK_PERIOD_MS);
                times++;
                printf("request times %d \n",times);
            }else{
                get_lte_lqi(rsrp,rsrq,rssi);
                ESP_LOGI(TAG, "rsrp %s  rsrq %s rssi %s ",rsrp,rsrq,rssi);
                times = 0;
            }
        }
    }
}

void user_task_init(void)
{
    xTaskCreatePinnedToCore(&uasr_task, "uasr_task", 1024*8, NULL, 11, NULL,0);
}
