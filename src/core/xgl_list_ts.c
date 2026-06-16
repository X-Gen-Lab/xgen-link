/**
 * \file            xgl_list_ts.c
 * \brief           Thread-safe intrusive list wrappers
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_list.h>
#include <stddef.h>

#ifdef XGL_THREAD_SAFE

#include <xgl/internal/xgl_mutex.h>

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
