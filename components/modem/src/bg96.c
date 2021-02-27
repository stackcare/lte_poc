// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "bg96.h"
#include "esp_modem.h"
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "../../main/include/database.h"

#define MODEM_RESULT_CODE_POWERDOWN "POWERED DOWN"

/**
 * @brief Macro defined for error checking
 *
 */
static const char *DCE_TAG = "bg96";
#define DCE_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                \
    {                                                                                 \
        if (!(a))                                                                     \
        {                                                                             \
            ESP_LOGE(DCE_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                            \
        }                                                                             \
    } while (0)


bg96_modem_dce_t *g_bg96_dce;

/**
 * @brief Handle response from AT+CSQ
 */
static esp_err_t bg96_handle_csq(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+CSQ", strlen("+CSQ"))) {
        /* store value of rssi and ber */
        uint32_t **csq = bg96_dce->priv_resource;
        /* +CSQ: <rssi>,<ber> */
        sscanf(line, "%*s%d,%d", csq[0], csq[1]);
        err = ESP_OK;
    }
    return err;
}

/**
 * @brief Handle response from AT+CBC
 */
static esp_err_t bg96_handle_cbc(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+CBC", strlen("+CBC"))) {
        /* store value of bcs, bcl, voltage */
        uint32_t **cbc = bg96_dce->priv_resource;
        /* +CBC: <bcs>,<bcl>,<voltage> */
        sscanf(line, "%*s%d,%d,%d", cbc[0], cbc[1], cbc[2]);
        err = ESP_OK;
    }
    return err;
}

/**
 * @brief Handle response from +++
 */
static esp_err_t bg96_handle_exit_data_mode(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_NO_CARRIER)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    }
    return err;
}

/**
 * @brief Handle response from ATD*99#
 */
static esp_err_t bg96_handle_atd_ppp(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_CONNECT)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    }
    return err;
}

/**
 * @brief Handle response from AT+CGMM
 */
static esp_err_t bg96_handle_cgmm(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        int len = snprintf(dce->name, MODEM_MAX_NAME_LENGTH, "%s", line);
        if (len > 2) {
            /* Strip "\r\n" */
            strip_cr_lf_tail(dce->name, len);
            err = ESP_OK;
        }
    }
    return err;
}

/**
 * @brief Handle response from AT+CGSN
 */
static esp_err_t bg96_handle_cgsn(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        int len = snprintf(dce->imei, MODEM_IMEI_LENGTH + 1, "%s", line);
        if (len > 2) {
            /* Strip "\r\n" */
            strip_cr_lf_tail(dce->imei, len);
            err = ESP_OK;
        }
    }
    return err;
}

/**
 * @brief Handle response from AT+CIMI
 */
static esp_err_t bg96_handle_cimi(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        int len = snprintf(dce->imsi, MODEM_IMSI_LENGTH + 1, "%s", line);
        if (len > 2) {
            /* Strip "\r\n" */
            strip_cr_lf_tail(dce->imsi, len);
            err = ESP_OK;
        }
    }
    return err;
}

/**
 * @brief Handle response from AT+COPS?
 */
static esp_err_t bg96_handle_cops(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else if (!strncmp(line, "+COPS", strlen("+COPS"))) {
        /* there might be some random spaces in operator's name, we can not use sscanf to parse the result */
        /* strtok will break the string, we need to create a copy */
        size_t len = strlen(line);
        char *line_copy = malloc(len + 1);
        strcpy(line_copy, line);
        /* +COPS: <mode>[, <format>[, <oper>]] */
        char *str_ptr = NULL;
        char *p[3];
        uint8_t i = 0;
        /* strtok will broke string by replacing delimiter with '\0' */
        p[i] = strtok_r(line_copy, ",", &str_ptr);
        while (p[i]) {
            p[++i] = strtok_r(NULL, ",", &str_ptr);
        }
        if (i >= 3) {
            int len = snprintf(dce->oper, MODEM_MAX_OPERATOR_LENGTH, "%s", p[2]);
            if (len > 2) {
                /* Strip "\r\n" */
                strip_cr_lf_tail(dce->oper, len);
                err = ESP_OK;
            }
        }
        free(line_copy);
    }
    return err;
}

/**
 * @brief Handle response from AT+QPOWD=1
 */
static esp_err_t bg96_handle_power_down(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = ESP_OK;
    } else if (strstr(line, MODEM_RESULT_CODE_POWERDOWN)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    }
    return err;
}

/**
 * @brief Get signal quality
 *
 * @param dce Modem DCE object
 * @param rssi received signal strength indication
 * @param ber bit error ratio
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_signal_quality(modem_dce_t *dce, uint32_t *rssi, uint32_t *ber)
{
    modem_dte_t *dte = dce->dte;
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    uint32_t *resource[2] = {rssi, ber};
    bg96_dce->priv_resource = resource;
    dce->handle_line = bg96_handle_csq;
    DCE_CHECK(dte->send_cmd(dte, "AT+CSQ\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "inquire signal quality failed", err);
    ESP_LOGD(DCE_TAG, "inquire signal quality ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get battery status
 *
 * @param dce Modem DCE object
 * @param bcs Battery charge status
 * @param bcl Battery connection level
 * @param voltage Battery voltage
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_battery_status(modem_dce_t *dce, uint32_t *bcs, uint32_t *bcl, uint32_t *voltage)
{
    modem_dte_t *dte = dce->dte;
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    uint32_t *resource[3] = {bcs, bcl, voltage};
    bg96_dce->priv_resource = resource;
    dce->handle_line = bg96_handle_cbc;
    DCE_CHECK(dte->send_cmd(dte, "AT+CBC\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "inquire battery status failed", err);
    ESP_LOGD(DCE_TAG, "inquire battery status ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}


/**
 * @brief Set Working Mode
 *
 * @param dce Modem DCE object
 * @param mode woking mode
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_set_working_mode(modem_dce_t *dce, modem_mode_t mode)
{
    modem_dte_t *dte = dce->dte;
    switch (mode) {
    case MODEM_COMMAND_MODE:
        dce->handle_line = bg96_handle_exit_data_mode;
        DCE_CHECK(dte->send_cmd(dte, "+++", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) == ESP_OK, "send command failed", err);
        DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "enter command mode failed", err);
        //ESP_LOGI(DCE_TAG, "enter command mode ok");
        dce->mode = MODEM_COMMAND_MODE;
        break;
    case MODEM_PPP_MODE:
        dce->handle_line = bg96_handle_atd_ppp;
        DCE_CHECK(dte->send_cmd(dte, "ATD*99#\r", MODEM_COMMAND_TIMEOUT_MODE_CHANGE) == ESP_OK, "send command failed", err);
        DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "enter ppp mode failed", err);
        ESP_LOGI(DCE_TAG, "enter ppp mode ok");
        dce->mode = MODEM_PPP_MODE;
        break;
    default:
        ESP_LOGW(DCE_TAG, "unsupported working mode: %d", mode);
        goto err;
        break;
    }
    return ESP_OK;
err:
    return ESP_FAIL;
}


/**
 * @brief Power down
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_power_down(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    dce->handle_line = bg96_handle_power_down;
    DCE_CHECK(dte->send_cmd(dte, "AT+QPOWD=1\r", MODEM_COMMAND_TIMEOUT_POWEROFF) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "power down failed", err);
    ESP_LOGD(DCE_TAG, "power down ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Handle response from AT+CGREG?
 */
static esp_err_t bg96_handle_cgreg(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    char * ptr_out = NULL;
    char * ptr_status = NULL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        printf("OK \n");
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        ptr_out= strtok_r(line, ",", &ptr_status);
        if(ptr_out && ptr_status){
            if (strstr(ptr_out,"+CGREG")) {
                printf("cgreg status %c \n",*ptr_status);
                dce->conStatus = *ptr_status;
                err = ESP_OK;
                //int len = snprintf(dce->imsi, MODEM_IMSI_LENGTH + 1, "%s", line);
                //strip_cr_lf_tail(dce->imei, len);
            }
        }
        
    }
    return ESP_OK;
}

/**
 * @brief Get DCE module name
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_lte_connect_status(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_cgreg;
    dte->send_cmd(dte, "AT+CGREG?\r", MODEM_COMMAND_TIMEOUT_DEFAULT+2500);
    ESP_LOGD(DCE_TAG, "get cgreg ok");
    return ESP_OK;

}

/**
 * @brief Handle response from AT+CGREG?
 */
static esp_err_t bg96_handle_simcard_cpin(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        printf("OK \n");
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        printf("CPIN  %s\n",line);
		if(strlen(line) == 5){
			return ESP_OK;	
		}
        if (strstr(line,"+CPIN") && strstr(line,"READY")) {
            dce->simCard_state = 1;
        }
    }
    return ESP_OK;
}

/**
 * @brief Get DCE module name
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_simCard_state(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_simcard_cpin;
    dte->send_cmd(dte, "AT+CPIN?\r", MODEM_COMMAND_TIMEOUT_DEFAULT);
    ESP_LOGD(DCE_TAG, "get cpin ok");
    return ESP_OK;
}



/**
 * @brief Handle response from AT+QENG="servingcell"
 */
static esp_err_t bg96_handle_servingcell(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        //printf("OK \n");
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        size_t len = strlen(line);
        char *line_copy = malloc(len + 1);
        strcpy(line_copy, line);
        uint8_t i = 0,j = 0;
        char *p[17] = {{NULL}},*ptr = NULL;
        memset(&dce->servingcell,0,sizeof(dce->servingcell));
        /* strtok will broke string by replacing delimiter with '\0' */
        ptr = strtok(line_copy, ",");
        while (ptr) {
            p[i] = ptr;
            ++i;
            ptr = strtok(NULL, ",");
        }
        //printf("servingcell  %s\n",line);
        if(p[13]){
            strcpy(dce->servingcell.rsrp,p[13]);
        }
        if(p[14]){
            strcpy(dce->servingcell.rsrq,p[14]);
        }
        if(p[15]){
            strcpy(dce->servingcell.rssi,p[15]);
        }
        
        
        /* if (i >= 3) {
             for(j = 0; j < i;j++)
             printf(" %s\n",p[j]);
        } */
        free(line_copy);

        //
        err = ESP_OK;
        
    }
    return err;
}

/**
 * @brief Get DCE servingcell
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
esp_err_t bg96_get_servingcell(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_servingcell;
    dte->send_cmd(dte, "AT+QENG=\"servingcell\"\r", MODEM_COMMAND_TIMEOUT_DEFAULT);
    ESP_LOGD(DCE_TAG, "get cpin ok");
    return ESP_OK;

}

/**
 * @brief Handle response from AT+QENG="servingcell"
 */
static esp_err_t bg96_handle_activeUartRate(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        
        printf("active uart rate OK \n");
        dce->uartRate_set_status = 2;
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        printf("servingcell  %s\n",line);
        err = ESP_OK;
        
    }
    return err;
}

/**
 * @brief Get DCE servingcell
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_active_uartRate(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_activeUartRate;
    dte->send_cmd(dte, "AT&W\r", MODEM_COMMAND_TIMEOUT_DEFAULT);
    ESP_LOGD(DCE_TAG, "set uart rate ok");
    return ESP_OK;
}

/**
 * @brief Handle response from AT+QENG="servingcell"
 */
static esp_err_t bg96_handle_setUartRate(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        dce->uartRate_set_status = 1;
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {
        printf("set uart rate  %s\n",line);
        err = ESP_OK;
        
    }
    return err;
}

/**
 * @brief Get DCE servingcell
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_set_uartRate(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_setUartRate;
    dte->send_cmd(dte, "AT+IPR=921600\r", MODEM_COMMAND_TIMEOUT_SET_UART_RATE);
    ESP_LOGD(DCE_TAG, "set uart rate ok");
    return ESP_OK;
}

/**
 * @brief Handle response from AT+QENG="servingcell"
 */
static esp_err_t bg96_handle_switch_pppMode(modem_dce_t *dce, const char *line)
{
    modem_dte_t *dte = dce->dte;
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    esp_err_t err = ESP_FAIL;
    //printf("switch_pppMode  %s\n",line);
    err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    dce->mode = MODEM_PPP_MODE;
    uart_disable_pattern_det_intr(esp_dte->uart_port);
    uart_enable_rx_intr(esp_dte->uart_port);
    err = ESP_OK;
        
    return ESP_OK;
}

/**
 * @brief Get DCE servingcell
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
esp_err_t bg96_switch_pppMode(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_switch_pppMode;
    dte->send_cmd(dte, "ATO\r", MODEM_COMMAND_TIMEOUT_SET_UART_RATE);
    //bg96_dce->parent.mode = MODEM_PPP_MODE;
    return ESP_OK;
}



/**
 * @brief Handle response from AT+QENG="servingcell"
 */
static esp_err_t bg96_handle_get_firmware_ver(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_OK;
       
    err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    printf("get firmware ver %s\n",line);
        
    return err;
}

/**
 * @brief Get DCE servingcell
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_firmware_ver(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_get_firmware_ver;
    dte->send_cmd(dte, "AT+QGMR\r", MODEM_COMMAND_TIMEOUT_SET_UART_RATE);
    ESP_LOGD(DCE_TAG, "get firmware_ver ok");
    return ESP_OK;
}


/**
 * @brief Handle response from AT+QENG="servingcell"
 */
static esp_err_t bg96_handle_get_iccid(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        dce->uartRate_set_status = 1;
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    } else {       
        
        memset(dce->iccid,0,sizeof(dce->iccid));
        
        strcpy(dce->iccid,line+8);
        err = ESP_OK;
        
    }
        
    return err;
}

/**
 * @brief Get DCE iccid
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_iccid(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_get_iccid;
    dte->send_cmd(dte, "AT+QCCID\r", MODEM_COMMAND_TIMEOUT_DEFAULT);
    ESP_LOGD(DCE_TAG, "get iccid ok");
    return ESP_OK;
}

/**
 * @brief Get DCE CGREG , if use LTE-CAT.M use CEREG 
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_module_name(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_cgmm;
    DCE_CHECK(dte->send_cmd(dte, "AT+CGMM\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(bg96_dce->parent.state == MODEM_STATE_SUCCESS, "get module name failed", err);
    ESP_LOGD(DCE_TAG, "get module name ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get DCE module IMEI number
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_imei_number(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_cgsn;
    DCE_CHECK(dte->send_cmd(dte, "AT+CGSN\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(bg96_dce->parent.state == MODEM_STATE_SUCCESS, "get imei number failed", err);
    ESP_LOGD(DCE_TAG, "get imei number ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get DCE module IMSI number
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_imsi_number(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_cimi;
    DCE_CHECK(dte->send_cmd(dte, "AT+CIMI\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(bg96_dce->parent.state == MODEM_STATE_SUCCESS, "get imsi number failed", err);
    ESP_LOGD(DCE_TAG, "get imsi number ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Get Operator's name
 *
 * @param bg96_dce bg96 object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t bg96_get_operator_name(bg96_modem_dce_t *bg96_dce)
{
    modem_dte_t *dte = bg96_dce->parent.dte;
    bg96_dce->parent.handle_line = bg96_handle_cops;
    DCE_CHECK(dte->send_cmd(dte, "AT+COPS?\r", MODEM_COMMAND_TIMEOUT_OPERATOR) == ESP_OK, "send command failed", err);
    DCE_CHECK(bg96_dce->parent.state == MODEM_STATE_SUCCESS, "get network operator failed", err);
    ESP_LOGD(DCE_TAG, "get network operator ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

/**
 * @brief Deinitialize BG96 object
 *
 * @param dce Modem DCE object
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on fail
 */
static esp_err_t bg96_deinit(modem_dce_t *dce)
{
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    if (dce->dte) {
        dce->dte->dce = NULL;
    }
    free(bg96_dce);
    return ESP_OK;
}

void change_ppp_uart_rate(modem_dte_t *dte)
{
    esp_modem_dte_t *esp_dte = __containerof(dte, esp_modem_dte_t, parent);
    ESP_LOGI(DCE_TAG, "set uart port %d ",esp_dte->uart_port);
    uart_set_baudrate(esp_dte->uart_port, 921600);
}

modem_dce_t *bg96_init(modem_dte_t *dte)
{
    DCE_CHECK(dte, "DCE should bind with a DTE", err);
    /* malloc memory for bg96_dce object */
    //bg96_modem_dce_t *bg96_dce = calloc(1, sizeof(bg96_modem_dce_t));
    g_bg96_dce = calloc(1, sizeof(bg96_modem_dce_t));
    DCE_CHECK(g_bg96_dce, "calloc bg96_dce failed", err);
    /* Bind DTE with DCE */
    g_bg96_dce->parent.dte = dte;
    dte->dce = &(g_bg96_dce->parent);
    /* Bind methods */
    g_bg96_dce->parent.handle_line = NULL;
    g_bg96_dce->parent.sync = esp_modem_dce_sync;
    g_bg96_dce->parent.echo_mode = esp_modem_dce_echo;
    g_bg96_dce->parent.store_profile = esp_modem_dce_store_profile;
    g_bg96_dce->parent.set_flow_ctrl = esp_modem_dce_set_flow_ctrl;
    g_bg96_dce->parent.define_pdp_context = esp_modem_dce_define_pdp_context;
    g_bg96_dce->parent.hang_up = esp_modem_dce_hang_up;
    g_bg96_dce->parent.get_signal_quality = bg96_get_signal_quality;
    g_bg96_dce->parent.get_battery_status = bg96_get_battery_status;
    g_bg96_dce->parent.set_working_mode = bg96_set_working_mode;
    g_bg96_dce->parent.power_down = bg96_power_down;
    g_bg96_dce->parent.switch_pppMode = bg96_switch_pppMode;
    g_bg96_dce->parent.deinit = bg96_deinit;
    unsigned char cpin_timesout = 0;

    while(1){
        
        if(g_bg96_dce->parent.simCard_state != 1){
            bg96_get_simCard_state(g_bg96_dce);
            vTaskDelay(500/portTICK_PERIOD_MS);
            cpin_timesout++;
            //if(cpin_timesout > 20){
             //   return NULL;
            //}
            //bg96_get_servingcell(g_bg96_dce);
            //vTaskDelay(500/portTICK_PERIOD_MS);
            continue;
        }
        #if 0
        if(db_get_lte_uart_rate() != 921600){ // at first, change 4G module uart rate to 921600
            if(g_bg96_dce->parent.uartRate_set_status == 0){
                bg96_set_uartRate(g_bg96_dce);
                vTaskDelay(500/portTICK_PERIOD_MS);
                continue;
            }else if(g_bg96_dce->parent.uartRate_set_status == 1){
                g_bg96_dce->parent.uartRate_set_status = 0;
                change_ppp_uart_rate(g_bg96_dce->parent.dte);
                db_set_lte_uart_rate();
                //bg96_active_uartRate(g_bg96_dce);
                //vTaskDelay(500/portTICK_PERIOD_MS);
                continue;
            }else if(g_bg96_dce->parent.uartRate_set_status == 2){
                g_bg96_dce->parent.uartRate_set_status = 0;
                change_ppp_uart_rate(g_bg96_dce->parent.dte);
                db_set_lte_uart_rate();
            }
        }
        
		#endif
        //bg96_get_firmware_ver(g_bg96_dce);
        bg96_get_lte_connect_status(g_bg96_dce);
		vTaskDelay(500/portTICK_PERIOD_MS);
        if(g_bg96_dce->parent.conStatus == '1' || g_bg96_dce->parent.conStatus == '5'){
            //bg96_get_servingcell(g_bg96_dce);
            //vTaskDelay(500/portTICK_PERIOD_MS);
            break;
        }
    }

    // /* Sync between DTE and DCE */
    // esp_modem_dce_sync(&(bg96_dce->parent));
    // /* Close echo */
    // esp_modem_dce_echo(&(bg96_dce->parent), false);
    // /* Get Module name */
    // bg96_get_module_name(bg96_dce);
    // /* Get IMEI number */
    // bg96_get_imei_number(bg96_dce);
    // /* Get IMSI number */
    // bg96_get_imsi_number(bg96_dce);
    // /* Get operator name */
    // bg96_get_operator_name(bg96_dce);
     return &(g_bg96_dce->parent);

err:
    return NULL;
}

void bg96_ppp_start(void)
{
    /* Sync between DTE and DCE */
    esp_modem_dce_sync(&(g_bg96_dce->parent));
    /* Close echo */
    esp_modem_dce_echo(&(g_bg96_dce->parent), false);
    /* Get Module name */
    bg96_get_module_name(g_bg96_dce);
    /* Get IMEI number */
    bg96_get_imei_number(g_bg96_dce);
    /* Get IMSI number */
    bg96_get_imsi_number(g_bg96_dce);
    /* Get operator name */
    bg96_get_operator_name(g_bg96_dce);
    /* Get sim card iccid */
    bg96_get_iccid(g_bg96_dce);
    
}

