#ifndef __ESP32_OTA_H__
#define __ESP32_OTA_H__

void esp32_ota_start(char * version);
void __attribute__((noreturn)) ota_task_fatal_error(void);
void ota_http_cleanup(void);
void ota_percent_report(char status,unsigned int pos);
#endif