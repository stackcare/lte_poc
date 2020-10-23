#include <stddef.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#include "esp_log.h"
#include "lwip/apps/sntp.h"

#include "lte_poc.h"

static const char *TAG = "Utils";

static const char* sntp_server_0 = "time.google.com";
static const char* sntp_server_1 = "0.pool.ntp.org";
static const char* sntp_server_2 = "1.pool.ntp.org";

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char*) sntp_server_0);
    sntp_setservername(1, (char*) sntp_server_1);
    sntp_setservername(2, (char*) sntp_server_2);
    sntp_init();
}

static bool _obtain_time()
{
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo;
    int retry = 0;
    const int retry_count = 20;
    char strftime_buf[64];
    timeinfo.tm_year = 0;

    //zb_set_led(YELLOW, ON);
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGD(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    //zb_set_led(YELLOW, OFF);

    if (retry == retry_count) {
      ESP_LOGE(TAG, "Failed to set time.");
      return false;
    } else {
      strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
      ESP_LOGI(TAG, "Current Time : %s", strftime_buf);
      return true;
    }
}

// won't return until it gets the time
void obtain_time()
{
    while (_obtain_time() == false) {
        ESP_LOGI(TAG,"Stopping SNTP module and retrying to get time.");
        sntp_stop();
    }
}
