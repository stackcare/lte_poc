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

static const char *TAG = "ota";

#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */
unsigned int binary_file_length = 0;
/*an ota data write buffer ready to write to the flash*/

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#define OTA_URL_SIZE 200

unsigned int g_filelen = 0;

char g_version[20] = {0};
esp_http_client_handle_t client;

void __attribute__((noreturn)) ota_task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    if(binary_file_length < g_filelen){
        //ota_percent_report(1,binary_file_length);
    }
    (void)vTaskDelete(NULL);
    while (1) {
        ;
    }
}

static void http_cleanup(esp_http_client_handle_t client)
{
    ESP_LOGE(TAG, "http_cleanup !!!!");
    vTaskDelay(100/portTICK_PERIOD_MS);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    client = NULL;
}

void ota_http_cleanup(void)
{
    http_cleanup(client);
}

static void print_sha256 (const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}




esp_err_t _ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGE(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGE(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // ESP_LOGE(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            if(strcmp("Content-Length",evt->header_key) == 0){

                g_filelen = atoi(evt->header_value);
                ESP_LOGE(TAG, "g_filelen %d ", g_filelen);
            }
            break;
        case HTTP_EVENT_ON_DATA:
            //ESP_LOGE(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
             if (!esp_http_client_is_chunked_response(evt->client)) {
                 // Write out data
                  //printf("%.*s", evt->data_len, (char*)evt->data);
             }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGE(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGE(TAG, "HTTP_EVENT_DISCONNECTED");
            http_cleanup(client);
            ota_task_fatal_error();
            int mbedtls_err = 0;
            
            break;
    }
    return ESP_OK;
}

static void ota_task(void *pvParameter)
{
    esp_err_t err;
    char urlTmp[100] = {0};
    static char ota_write_data[BUFFSIZE + 1] = { 0 };
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    //ESP_LOGE(TAG, "Starting OTA example");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGE(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGE(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    // ESP_LOGE(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
    //          running->type, running->subtype, running->address);

    
    strcpy(urlTmp,"https://storage.googleapis.com/gateway_builds/hubv_images/test_ota/pppos_client.bin");
    //strcpy(urlTmp,"http://httpbin.org/get/");
    //strcat(urlTmp,"oldManCare_");
    //strcat(urlTmp,g_version);
    //strcat(urlTmp,".bin");


    esp_http_client_config_t config = {
        .url = urlTmp,
        .timeout_ms = 5000000,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _ota_http_event_handler,
    };

    client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        ota_task_fatal_error();
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        ota_task_fatal_error();
    }
    esp_http_client_fetch_headers(client);

    update_partition = esp_ota_get_next_update_partition(NULL);
    // ESP_LOGE(TAG, "Writing to partition subtype %d at offset 0x%x",
    //          update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    
    /*deal with all receive packet*/
    bool image_header_was_checked = false;
    while (1) {
        if(client == NULL){
            ESP_LOGE(TAG, "client == NULL");
            break;
        }
        esp_task_wdt_reset();
        int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            http_cleanup(client);
            ota_task_fatal_error();
        } else if (data_read > 0) {
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGE(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGE(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                            ESP_LOGE(TAG, "New version is the same as invalid version.");
                            ESP_LOGE(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                            ESP_LOGE(TAG, "The firmware has been rolled back to the previous version.");
                            http_cleanup(client);
                        }
                    }
                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        http_cleanup(client);
                        ota_task_fatal_error();
                    }
                    ESP_LOGE(TAG, "esp_ota_begin succeeded");
                } else {
                    ESP_LOGE(TAG, "received package is not fit len");
                    http_cleanup(client);
                    ota_task_fatal_error();
                }
            }
            //ESP_LOGE(TAG, "Written image length %d %d",data_read, binary_file_length);
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write (%s)", esp_err_to_name(err));
                http_cleanup(client);
                ota_task_fatal_error();
            }
            binary_file_length += data_read;
            ota_percent_report(0,binary_file_length);
            
        } else if (data_read == 0) {
            //esp_task_wdt_reset();
            ESP_LOGE(TAG, "data_read == 0");
            vTaskDelay(100/portTICK_PERIOD_MS);
           /*
            * As esp_http_client_read never returns negative error code, we rely on
            * `errno` to check for underlying transport connectivity closure if any
            */
             if (errno == ECONNRESET || errno == ENOTCONN) {
                ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
                break;
            } 
            if (esp_http_client_is_complete_data_received(client) == true) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            } 
        }
    }
    
    ESP_LOGE(TAG, "Total Write binary data length: %d", binary_file_length);
    if (esp_http_client_is_complete_data_received(client) != true) {
        ESP_LOGE(TAG, "Error in receiving complete file");
        http_cleanup(client);
        ota_task_fatal_error();
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        }
        ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        http_cleanup(client);
        ota_task_fatal_error();
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        http_cleanup(client);
        ota_task_fatal_error();
    }
    ESP_LOGE(TAG, "Prepare to restart system!");
    esp_restart();
    return ;
}


void esp32_ota_start(char * version)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for the partition table
    partition.address   = ESP_PARTITION_TABLE_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
    partition.type      = ESP_PARTITION_TYPE_DATA;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for the partition table: ");

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // run diagnostic function ...
            // bool diagnostic_is_ok = diagnostic();
            // if (diagnostic_is_ok) {
            //     ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
            //     esp_ota_mark_app_valid_cancel_rollback();
            // } else {
            //     ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
            //     esp_ota_mark_app_invalid_rollback_and_reboot();
            // }
        }
    }
    strcpy(g_version,version);

    xTaskCreatePinnedToCore(&ota_task, "ota_task", 1024*8, NULL, 10, NULL,1);
}

void ota_percent_report(char status,unsigned int pos)
{
    char tmp = 0;
    static char i = 0;
    if(g_filelen > 0){
        tmp = ((pos * 1.0)/g_filelen)*100;
        if(i == 0){
            i = 1;
           
        }else{
            if(tmp - i > 0){
                i = tmp;
                if(tmp == 100){
                    i = 0;
                }
                ESP_LOGE(TAG, "ota download percent %d/%%", tmp);
            }
        }
    }
}
