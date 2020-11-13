//
//  Copyright © 2020 Stack Care Inc. All rights reserved.
//

#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "lte_poc.h"

static const char *TAG = "linked_list";

static node_t *get_last(node_t *list)
{
    node_t *last = list;
    while (last != NULL && last->next != NULL) {
        last = last->next;
    }
    return last;
}

// add a new node at the end
void ll_append(node_t **list, void *value)
{
    node_t *new_node = (node_t *) malloc(sizeof(node_t));
    if (new_node == NULL) {
        ESP_LOGE(TAG, "failed to create a new node");
        return;
    }

    new_node->value = value;
    new_node->next = NULL;

    node_t *last_node = get_last(*list);
    if (last_node == NULL) {
        *list = new_node;
    } else {
        last_node->next = new_node;
    }
}

bool ll_is_empty(node_t **list)
{
    return (*list == NULL);
}

// pop a value from front
void *ll_pop(node_t **list)
{
    node_t *head = *list;
    if (head == NULL) {
        return NULL;
    } else {
        *list = head->next;
        void *result = head->value;
        free(head);
        return result;
    }
}

void ll_free(node_t **list)
{
    while (ll_is_empty(list) == false) {
        void *value = ll_pop(list);
        free(value);
    }
}
