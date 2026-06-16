/**
 * \file            xgl_list.c
 * \brief           Intrusive doubly-linked list implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_list.h>
#include <stddef.h>

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
