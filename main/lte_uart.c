//
//  Copyright © 2020 Stack Care Inc. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_spi_flash.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "lte_poc.h"

#define RX_BUF_SIZE 511
#define UART_EVENT_QUEUE_SIZE 20

#define BIT_SIGNAL_QUALITY ( 1 << 0 )
#define BIT_CCID           ( 1 << 1 )
#define BIT_CEREG          ( 1 << 2 )

// used to sync with the listen task
#define BIT_SHOULD_LISTEN  ( 1 << 5 )
#define BIT_LISTEN_STOPPED ( 1 << 6 )

//#define AT_GET_CGREG "AT+CGREG?\r"

static const char *TAG = "LTE";

static QueueHandle_t s_uart_event_queue;
static TaskHandle_t s_listen_task = NULL;
static EventGroupHandle_t s_event_group = NULL;
static int s_cereg_status = 0;

// Used to send AT commands sequentially.
// Because sending multiple commands at once doesn't seem to work
static node_t *s_command_queue = NULL;

static LteInfo s_lte_info;

static void init_uart(void)
{
    ESP_LOGW(TAG, "initializing LTE UART");

    uart_config_t lte_uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_NUM_2, &lte_uart_config);
    uart_set_pin(UART_NUM_2, CONFIG_EXAMPLE_UART_MODEM_TX_PIN, CONFIG_EXAMPLE_UART_MODEM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_2, CONFIG_EXAMPLE_UART_RX_BUFFER_SIZE, CONFIG_EXAMPLE_UART_TX_BUFFER_SIZE, UART_EVENT_QUEUE_SIZE, &s_uart_event_queue, 0);

    /* Set pattern interrupt, used to detect the end of a line */
    uart_enable_pattern_det_baud_intr(UART_NUM_2, '\n', 1, 15, 0, 0);
    /* Set pattern queue size */
    uart_pattern_queue_reset(UART_NUM_2, UART_EVENT_QUEUE_SIZE);
    uart_flush(UART_NUM_2);

    ESP_LOGI(TAG, "LTE UART init done");
}

static int send_to_lte(char *data)
{
    int sent = 0;
    sent = uart_write_bytes(UART_NUM_2, data, strlen(data));
    return sent;
}

static void enqueue_command(const char *cmd)
{
    char *cmd_copy = NULL;
    asprintf(&cmd_copy, "%s", cmd);
    ll_append(&s_command_queue, cmd_copy);
}

static void send_next_command()
{
    const char *cmd = (const char *) ll_pop(&s_command_queue);
    if (cmd != NULL) {
        lte_send_command(cmd);
        free((void *)cmd);
    }
}

static void read_from_lte()
{
    char buf[RX_BUF_SIZE+1];

    int pos = uart_pattern_pop_pos(UART_NUM_2);
    if (pos != -1) {
        /* read one line(include '\n') */
        int read_len = uart_read_bytes(UART_NUM_2, (uint8_t *)buf, pos + 1, 100 / portTICK_PERIOD_MS);
        /* make sure the line is a standard string by replacing '\n' */
        buf[read_len - 1] = '\0';
        ESP_LOGI(TAG, "| %s", buf);
        int rssi = 0;
        int ber = 0;
        int n = sscanf(buf, "+CSQ: %d,%d", &rssi, &ber);
        if (n == 2) {
            // category reference: https://m2msupport.net/m2msupport/atcsq-signal-quality/
            if (rssi == 99) {
                s_lte_info.signal_quality = LTE_QUALITY_UNKNOWN;
            } else if (rssi < 10) {
                s_lte_info.signal_quality = LTE_QUALITY_POOR;
            } else if (rssi < 15) {
                s_lte_info.signal_quality = LTE_QUALITY_OK;
            } else if (rssi < 20) {
                s_lte_info.signal_quality = LTE_QUALITY_GOOD;
            } else {
                s_lte_info.signal_quality = LTE_QUALITY_EXCELLENT;
            }
            ESP_LOGI(TAG, "signal quality determined: %d", s_lte_info.signal_quality);
            xEventGroupSetBits(s_event_group, BIT_SIGNAL_QUALITY);
            send_next_command();
            return;
        }
        char ccid[32];
        n = sscanf(buf, "+QCCID: %s", ccid);
        if (n == 1) {
            // Any 'F' characters at the end of the string should be stripped
            // https://support.hologram.io/hc/en-us/articles/360043471813-Why-does-my-modem-show-my-SIM-number-with-an-extra-F-at-the-end-
            int len = strlen(ccid);
            while (len > 0) {
                char last = ccid[len - 1];
                if (last != 'f' && last != 'F') break;
                ccid[len - 1] = '\0';
                len -= 1;
            }
            strcpy(s_lte_info.ccid, ccid);
            ESP_LOGI(TAG, "SIM number determined: %s", s_lte_info.ccid);
            xEventGroupSetBits(s_event_group, BIT_CCID);
            send_next_command();
            return;
        }
        int status = 0;
        int act = 0; // Access Technology
        n = sscanf(buf, "+CEREG: %d,%d", &status, &act);
        if (n == 2) {
            ESP_LOGI(TAG, "CEREG network registration status: %d", status);
            s_cereg_status = status;
            xEventGroupSetBits(s_event_group, BIT_CEREG);
            send_next_command();
            return;
        }
    } else {
        ESP_LOGW(TAG, "Pattern Queue Size too small");
        uart_flush_input(UART_NUM_2);
    }
}

static void listen_task(void *pvParameters)
{
    uart_event_t event;
    while (1) {
        if (xQueueReceive(s_uart_event_queue, &event, pdMS_TO_TICKS(200))) {
            switch (event.type) {
            case UART_DATA:
                break;
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "HW FIFO Overflow");
                uart_flush(UART_NUM_2);
                xQueueReset(s_uart_event_queue);
                break;
            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "Ring Buffer Full");
                uart_flush(UART_NUM_2);
                xQueueReset(s_uart_event_queue);
                break;
            case UART_BREAK:
                ESP_LOGW(TAG, "Rx Break");
                break;
            case UART_PARITY_ERR:
                ESP_LOGE(TAG, "Parity Error");
                break;
            case UART_FRAME_ERR:
                ESP_LOGE(TAG, "Frame Error");
                break;
            case UART_PATTERN_DET:
                read_from_lte();
                break;
            default:
                ESP_LOGW(TAG, "unknown uart event type: %d", event.type);
                break;
            }
        }

        EventBits_t bits = xEventGroupGetBits(s_event_group);
        if ((bits & BIT_SHOULD_LISTEN) == 0) {
            break;
        }
    }

    xEventGroupSetBits(s_event_group, BIT_LISTEN_STOPPED);

    vTaskDelete(NULL);
}

void lte_uart_start(void)
{
    if (s_listen_task != NULL) {
        ESP_LOGW(TAG, "already listening on LTE UART");
        return;
    }

    s_event_group = xEventGroupCreate();
    xEventGroupSetBits(s_event_group, BIT_SHOULD_LISTEN);
    xEventGroupClearBits(s_event_group, BIT_LISTEN_STOPPED);

    init_uart();
    xTaskCreate(&listen_task, "lte_listen_task", 1024 * 6, NULL, 6, &s_listen_task);
    lte_send_command("I"); // send ATI command
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void lte_stop_listen(void)
{
    if (s_listen_task == NULL) {
        ESP_LOGW(TAG, "not listening on LTE UART");
        return;
    }

    xEventGroupClearBits(s_event_group, BIT_SHOULD_LISTEN);
    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                                           BIT_LISTEN_STOPPED,
                                           pdFALSE, // not to clear bits when return. Doesn't matter
                                           pdTRUE, // waiting for all bits. Doesn't matter here
                                           5000 / portTICK_PERIOD_MS); // 5 seconds should be plenty

    if ((bits & BIT_LISTEN_STOPPED) == 0) {
        ESP_LOGE(TAG, "listen_task didn't respond for stop request");
    }

    s_listen_task = NULL;
    uart_driver_delete(UART_NUM_2);
    vEventGroupDelete(s_event_group);
    s_event_group = NULL;
    ESP_LOGD(TAG, "LTE UART stopped");
}

void lte_send_command(const char *cmd)
{
    if (s_listen_task == NULL) {
        ESP_LOGW(TAG, "not listening on LTE UART");
        return;
    }

    char *command = NULL;
    if (cmd == NULL) {
        command = "AT\r";
    } else {
        asprintf(&command, "AT%s\r", cmd);
    }
    if (command == NULL) {
        ESP_LOGE(TAG, "failed to construct AT command");
        return;
    }

    send_to_lte(command);
    ESP_LOGI(TAG, "sent data: %s", command);
}

LteInfo lte_get_info()
{
    s_lte_info.signal_quality = LTE_QUALITY_UNKNOWN;
    s_lte_info.ccid[0] = '\0';
    xEventGroupClearBits(s_event_group, BIT_SIGNAL_QUALITY | BIT_CCID);
    enqueue_command("+CSQ"); // request signal quality
    enqueue_command("+QCCID"); // request CCID value
    ESP_LOGI(TAG, "requesting LTE info...");
    send_next_command();
    EventBits_t bits;
    bits = xEventGroupWaitBits(s_event_group,
                    BIT_SIGNAL_QUALITY | BIT_CCID,
                    pdFALSE, // not to clear bits when return. Doesn't matter
                    pdTRUE,  // waiting for both bits
                    5000 / portTICK_PERIOD_MS); // If this is too long, BLE communication might time out (30 seconds did)
    ESP_LOGI(TAG, "wait on LTE info is over");

    if ((bits & BIT_SIGNAL_QUALITY) == 0) {
        ESP_LOGW(TAG, "signal quality request didn't return a value");
    }
    if ((bits & BIT_CCID) == 0) {
        ESP_LOGW(TAG, "CCID request didn't return a value");
    }

    return s_lte_info;
}

void lte_register_network()
{
    enqueue_command("+CEREG=1"); // enable registration status
    send_next_command();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    for (;;) {
        xEventGroupClearBits(s_event_group, BIT_CEREG);
        enqueue_command("+CEREG?"); // request registration status
        ESP_LOGI(TAG, "requesting LTE network registration...");
        send_next_command();
        EventBits_t bits;
        bits = xEventGroupWaitBits(s_event_group,
                                   BIT_CEREG,
                                   pdFALSE, // not to clear bits when return. Doesn't matter
                                   pdTRUE,  // waiting for both bits. Doesn't matter
                                   5000 / portTICK_PERIOD_MS);

        if ((bits & BIT_CEREG) == 0) {
            ESP_LOGW(TAG, "not received CEREG response");
        } else if (s_cereg_status == 1) {
            ESP_LOGI(TAG, "LTE network registered");
            break;
        } else {
            ESP_LOGI(TAG, "LTE network not registered yet");
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
