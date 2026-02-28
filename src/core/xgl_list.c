/**
 * \file            xgl_list.c
 * \brief           Intrusive doubly-linked list implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_list.h>
#include <stddef.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* List Initialization                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize a list
 */
void xgl_list_init(xgl_list_t* list) {
    if (list == NULL) {
        return;
    }
    
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

/**
 * \brief           Initialize a list node
 */
void xgl_list_node_init(xgl_list_node_t* node) {
    if (node == NULL) {
        return;
    }
    
    node->next = NULL;
    node->prev = NULL;
}

/*---------------------------------------------------------------------------*/
/* List Query Operations                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Check if list is empty
 */
bool xgl_list_is_empty(const xgl_list_t* list) {
    if (list == NULL) {
        return true;
    }
    
    return list->head == NULL;
}

/**
 * \brief           Get number of nodes in list
 */
size_t xgl_list_count(const xgl_list_t* list) {
    if (list == NULL) {
        return 0;
    }
    
    return list->count;
}

/*---------------------------------------------------------------------------*/
/* List Insertion Operations                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Insert node at head of list
 */
void xgl_list_insert_head(xgl_list_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }
    
    node->prev = NULL;
    node->next = list->head;
    
    if (list->head != NULL) {
        list->head->prev = node;
    } else {
        /* List was empty, node is also tail */
        list->tail = node;
    }
    
    list->head = node;
    list->count++;
}

/**
 * \brief           Insert node at tail of list
 */
void xgl_list_insert_tail(xgl_list_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }
    
    node->next = NULL;
    node->prev = list->tail;
    
    if (list->tail != NULL) {
        list->tail->next = node;
    } else {
        /* List was empty, node is also head */
        list->head = node;
    }
    
    list->tail = node;
    list->count++;
}

/**
 * \brief           Insert node after specified node
 */
void xgl_list_insert_after(xgl_list_t* list, xgl_list_node_t* pos,
                          xgl_list_node_t* node) {
    if (list == NULL || pos == NULL || node == NULL) {
        return;
    }
    
    node->prev = pos;
    node->next = pos->next;
    
    if (pos->next != NULL) {
        pos->next->prev = node;
    } else {
        /* pos was tail, node is new tail */
        list->tail = node;
    }
    
    pos->next = node;
    list->count++;
}

/**
 * \brief           Insert node before specified node
 */
void xgl_list_insert_before(xgl_list_t* list, xgl_list_node_t* pos,
                           xgl_list_node_t* node) {
    if (list == NULL || pos == NULL || node == NULL) {
        return;
    }
    
    node->next = pos;
    node->prev = pos->prev;
    
    if (pos->prev != NULL) {
        pos->prev->next = node;
    } else {
        /* pos was head, node is new head */
        list->head = node;
    }
    
    pos->prev = node;
    list->count++;
}

/*---------------------------------------------------------------------------*/
/* List Removal Operations                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Remove node from list
 */
void xgl_list_remove(xgl_list_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }
    
    /* Update previous node's next pointer */
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        /* Node was head */
        list->head = node->next;
    }
    
    /* Update next node's prev pointer */
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        /* Node was tail */
        list->tail = node->prev;
    }
    
    /* Clear node pointers */
    node->next = NULL;
    node->prev = NULL;
    
    list->count--;
}

/**
 * \brief           Remove and return head node
 */
xgl_list_node_t* xgl_list_remove_head(xgl_list_t* list) {
    if (list == NULL || list->head == NULL) {
        return NULL;
    }
    
    xgl_list_node_t* node = list->head;
    xgl_list_remove(list, node);
    return node;
}

/**
 * \brief           Remove and return tail node
 */
xgl_list_node_t* xgl_list_remove_tail(xgl_list_t* list) {
    if (list == NULL || list->tail == NULL) {
        return NULL;
    }
    
    xgl_list_node_t* node = list->tail;
    xgl_list_remove(list, node);
    return node;
}

/*---------------------------------------------------------------------------*/
/* List Access Operations                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get head node without removing
 */
xgl_list_node_t* xgl_list_peek_head(const xgl_list_t* list) {
    if (list == NULL) {
        return NULL;
    }
    
    return list->head;
}

/**
 * \brief           Get tail node without removing
 */
xgl_list_node_t* xgl_list_peek_tail(const xgl_list_t* list) {
    if (list == NULL) {
        return NULL;
    }
    
    return list->tail;
}

/**
 * \brief           Get next node in list
 */
xgl_list_node_t* xgl_list_next(const xgl_list_node_t* node) {
    if (node == NULL) {
        return NULL;
    }
    
    return node->next;
}

/**
 * \brief           Get previous node in list
 */
xgl_list_node_t* xgl_list_prev(const xgl_list_node_t* node) {
    if (node == NULL) {
        return NULL;
    }
    
    return node->prev;
}

/*---------------------------------------------------------------------------*/
/* Thread-Safe Variants                                                      */
/*---------------------------------------------------------------------------*/

#ifdef XGL_THREAD_SAFE

#include "xgl_mutex.h"

/**
 * \brief           Initialize thread-safe list
 */
int xgl_list_ts_init(xgl_list_ts_t* list) {
    if (list == NULL) {
        return -1;
    }
    
    xgl_list_init(&list->list);
    return xgl_mutex_init(&list->mutex);
}

/**
 * \brief           Destroy thread-safe list
 */
void xgl_list_ts_destroy(xgl_list_ts_t* list) {
    if (list == NULL) {
        return;
    }
    
    xgl_mutex_destroy(&list->mutex);
}

/**
 * \brief           Check if thread-safe list is empty
 */
bool xgl_list_ts_is_empty(xgl_list_ts_t* list) {
    bool result;
    
    if (list == NULL) {
        return true;
    }
    
    xgl_mutex_lock(&list->mutex);
    result = xgl_list_is_empty(&list->list);
    xgl_mutex_unlock(&list->mutex);
    
    return result;
}

/**
 * \brief           Get number of nodes in thread-safe list
 */
size_t xgl_list_ts_count(xgl_list_ts_t* list) {
    size_t count;
    
    if (list == NULL) {
        return 0;
    }
    
    xgl_mutex_lock(&list->mutex);
    count = xgl_list_count(&list->list);
    xgl_mutex_unlock(&list->mutex);
    
    return count;
}

/**
 * \brief           Insert node at head of thread-safe list
 */
void xgl_list_ts_insert_head(xgl_list_ts_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }
    
    xgl_mutex_lock(&list->mutex);
    xgl_list_insert_head(&list->list, node);
    xgl_mutex_unlock(&list->mutex);
}

/**
 * \brief           Insert node at tail of thread-safe list
 */
void xgl_list_ts_insert_tail(xgl_list_ts_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }
    
    xgl_mutex_lock(&list->mutex);
    xgl_list_insert_tail(&list->list, node);
    xgl_mutex_unlock(&list->mutex);
}

/**
 * \brief           Remove node from thread-safe list
 */
void xgl_list_ts_remove(xgl_list_ts_t* list, xgl_list_node_t* node) {
    if (list == NULL || node == NULL) {
        return;
    }
    
    xgl_mutex_lock(&list->mutex);
    xgl_list_remove(&list->list, node);
    xgl_mutex_unlock(&list->mutex);
}

/**
 * \brief           Remove and return head node from thread-safe list
 */
xgl_list_node_t* xgl_list_ts_remove_head(xgl_list_ts_t* list) {
    xgl_list_node_t* node;
    
    if (list == NULL) {
        return NULL;
    }
    
    xgl_mutex_lock(&list->mutex);
    node = xgl_list_remove_head(&list->list);
    xgl_mutex_unlock(&list->mutex);
    
    return node;
}

/**
 * \brief           Remove and return tail node from thread-safe list
 */
xgl_list_node_t* xgl_list_ts_remove_tail(xgl_list_ts_t* list) {
    xgl_list_node_t* node;
    
    if (list == NULL) {
        return NULL;
    }
    
    xgl_mutex_lock(&list->mutex);
    node = xgl_list_remove_tail(&list->list);
    xgl_mutex_unlock(&list->mutex);
    
    return node;
}

/**
 * \brief           Get head node without removing from thread-safe list
 */
xgl_list_node_t* xgl_list_ts_peek_head(xgl_list_ts_t* list) {
    xgl_list_node_t* node;
    
    if (list == NULL) {
        return NULL;
    }
    
    xgl_mutex_lock(&list->mutex);
    node = xgl_list_peek_head(&list->list);
    xgl_mutex_unlock(&list->mutex);
    
    return node;
}

/**
 * \brief           Get tail node without removing from thread-safe list
 */
xgl_list_node_t* xgl_list_ts_peek_tail(xgl_list_ts_t* list) {
    xgl_list_node_t* node;
    
    if (list == NULL) {
        return NULL;
    }
    
    xgl_mutex_lock(&list->mutex);
    node = xgl_list_peek_tail(&list->list);
    xgl_mutex_unlock(&list->mutex);
    
    return node;
}

#endif /* XGL_THREAD_SAFE */
