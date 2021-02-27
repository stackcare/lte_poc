
#include "include/database.h"

#define LTE_STORAGE_NAMESPACE  "lte"

#define LTE_UART_RATE_KEY  "lte_rate"
static const char *TAG = "database";

void database_init(void)
{
    size_t value_len = sizeof(size_t);
    unsigned int rate = 0;
    nvs_get_lte_basic(LTE_UART_RATE_KEY,(char * )&rate, &value_len);
    
    if(rate == 0){
        rate = 115200;
        nvs_set_lte_basic(LTE_UART_RATE_KEY,(char * )&rate, &value_len);
    }
    ESP_LOGI(TAG, "rate %d ",rate);
}

void db_set_lte_uart_rate(void)
{
    size_t value_len = sizeof(size_t);
    size_t rate = 921600;
    nvs_set_lte_basic(LTE_UART_RATE_KEY,(char * )&rate, &value_len);
}

unsigned int db_get_lte_uart_rate(void)
{
    size_t value_len = sizeof(size_t);
    unsigned int  rate = 0;
    nvs_get_lte_basic(LTE_UART_RATE_KEY,(char * )&rate, &value_len);
    return rate;
}

esp_err_t nvs_set_lte_basic(char * key,char * value, size_t value_len)
{
	esp_err_t err;
	nvs_handle lte_handle;
	err = nvs_open(LTE_STORAGE_NAMESPACE, NVS_READWRITE, &lte_handle);
	if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open err %0x ",err);

        return err;
    }
	err = nvs_set_blob(lte_handle, key, value, value_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob err %0x ",err);
        return err;
    }
	err = nvs_commit(lte_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit err %0x ",err);
        return err;
    }
    // Close
    nvs_close(lte_handle);
	return err;
}

esp_err_t nvs_get_lte_basic(char * key,char * value, size_t * value_len)
{
	esp_err_t err;
	nvs_handle lte_handle;
	err = nvs_open(LTE_STORAGE_NAMESPACE, NVS_READWRITE, &lte_handle);
	if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open err %0x ",err);

        return err;
    }
	err = nvs_get_blob(lte_handle, key, value, (size_t *)value_len);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_blob err %0x ",err);
        return err;
    }
    
    // Close
    nvs_close(lte_handle);
	return err;
}


esp_err_t nvs_clear_device_lte_with_key(char * key)
{
    nvs_handle lte_handle;
    esp_err_t err = nvs_open(LTE_STORAGE_NAMESPACE, NVS_READWRITE, &lte_handle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "%s: failed to open NVS phy namespace (0x%x)", __func__, err);
        return err;
    }else{
        err = nvs_erase_key(lte_handle, key);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: failed to erase NVS phy namespace (0x%x)", __func__, err);
        }else {
            err = nvs_commit(lte_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: failed to commit NVS phy namespace (0x%x)", __func__, err);
            }
        }
    }
    nvs_close(lte_handle);
    return err;
}


