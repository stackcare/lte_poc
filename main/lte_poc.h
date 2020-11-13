//
//  Copyright © 2020 Stack Care Inc. All rights reserved.
//

#pragma once

// linked_list.c

typedef struct node {
    void *value;
    struct node *next;
} node_t;

void ll_append(node_t **list, void *value);
bool ll_is_empty(node_t **list);
void *ll_pop(node_t **list);
void ll_free(node_t **list);

// lte_uart.c

enum {
    LTE_QUALITY_UNKNOWN = 0,
    LTE_QUALITY_POOR,
    LTE_QUALITY_OK,
    LTE_QUALITY_GOOD,
    LTE_QUALITY_EXCELLENT
};

typedef struct {
    int signal_quality;
    char ccid[32];
} LteInfo;

void lte_uart_start(void);
void lte_stop_listen(void);
void lte_send_command(const char *cmd);
LteInfo lte_get_info();
void lte_register_network();

// http_test.c

esp_err_t run_http_test();
