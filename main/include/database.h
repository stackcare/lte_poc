#ifndef __ESP32_DATABASE_C_H__
#define __ESP32_DATABASE_C_H__
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

void database_init(void);
unsigned int db_get_lte_uart_rate(void);
void db_set_lte_uart_rate(void);
esp_err_t nvs_set_lte_basic(char * key,char * value, size_t value_len);
esp_err_t nvs_get_lte_basic(char * key,char * value, size_t * value_len);

#endif // !__ESP32_DATABASE_H__
