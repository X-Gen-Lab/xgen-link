/**
 * \file            xgl_list.c
 * \brief           Non-owning list operations shared by protocol queues
 * \author          X-Gen Lab
 */

#include <internal/xgl_list.h>

void xgl_list_init(xgl_list_t* list) {
    if (list != NULL) {
        *list = (xgl_list_t){0};
    }
}

void xgl_list_node_init(xgl_list_node_t* node) {
    if (node != NULL) {
        *node = (xgl_list_node_t){0};
    }
}

bool xgl_list_is_empty(const xgl_list_t* list) {
    return list == NULL || list->head == NULL;
}

size_t xgl_list_count(const xgl_list_t* list) {
    return list == NULL ? 0U : list->count;
}

void xgl_list_insert_tail(xgl_list_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }

    node->next = NULL;
    node->prev = list->tail;
    if (list->tail != NULL) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    ++list->count;
}

void xgl_list_remove(xgl_list_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }

    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    node->next = NULL;
    node->prev = NULL;
    --list->count;
}

xgl_list_node_t* xgl_list_remove_head(xgl_list_t* list) {
    if (list == NULL || list->head == NULL) {
        return NULL;
    }

    xgl_list_node_t* node = list->head;
    xgl_list_remove(list, node);
    return node;
}
