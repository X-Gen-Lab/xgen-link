/**
 * \file            xgl_fragment.c
 * \brief           Reassembly manager lifecycle, maintenance and message
 * release
 */

#include "xgl_fragment_internal.h"
#include <xgl/internal/xgl_fragment.h>

/*---------------------------------------------------------------------------*/
/* Fragmentation Manager Functions                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize fragmentation manager
 */
xgl_error_t xgl_fragment_init(xgl_fragment_manager_t* manager,
                              size_t max_reassembly_buffers,
                              uint32_t reassembly_timeout_ms,
                              const xgm_allocator_t* allocator) {
    if (manager == NULL || !xgm_allocator_is_valid(allocator)) {
        return XGL_ERR_NULL_POINTER;
    }

    if (max_reassembly_buffers == 0U || reassembly_timeout_ms > INT32_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Initialize reassembly list */
    xgct_list_init(&manager->reassembly_list);

    /* Store configuration */
    manager->max_reassembly_buffers = max_reassembly_buffers;
    manager->reassembly_timeout_ms = reassembly_timeout_ms;
    manager->allocator = allocator;
    manager->data_allocator = allocator;
    manager->max_message_size = 0;
    manager->max_reassembly_bytes = 0;
    manager->current_reassembly_bytes = 0;

    return XGL_OK;
}

xgl_error_t xgl_fragment_set_limits(xgl_fragment_manager_t* manager,
                                    size_t max_message_size,
                                    size_t max_reassembly_bytes) {
    if (manager == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (max_reassembly_bytes != 0U && max_message_size > max_reassembly_bytes) {
        return XGL_ERR_INVALID_PARAM;
    }

    manager->max_message_size = max_message_size;
    manager->max_reassembly_bytes = max_reassembly_bytes;

    return XGL_OK;
}

/**
 * \brief           Destroy fragmentation manager
 */
void xgl_fragment_destroy(xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    /* Clear all reassembly buffers */
    xgl_fragment_clear_reassembly(manager);
}

/**
 * \brief           Process reassembly timeouts
 */
uint32_t xgl_fragment_process_timeouts(xgl_fragment_manager_t* manager,
                                       uint32_t current_time_ms) {
    if (manager == NULL) {
        return 0;
    }

    uint32_t timeout_count = 0;

    /* Iterate through reassembly buffers */
    xgct_list_node_t* node;
    xgct_list_node_t* tmp;
    XGCT_LIST_FOR_EACH_SAFE(&manager->reassembly_list, node, tmp) {
        xgl_reassembly_buffer_t* buffer =
            /* Intrusive node membership is established by the owning list. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        /* Skip if first fragment hasn't been received yet */
        if (buffer->received_bytes == 0U || buffer->timeout_ms == 0U) {
            continue;
        }

        /* Calculate elapsed time */
        uint32_t elapsed_ms = current_time_ms - buffer->first_fragment_time;

        /* Check if timeout occurred */
        if (elapsed_ms >= buffer->timeout_ms) {
            /* Remove from list */
            xgct_list_remove(&manager->reassembly_list, node);

            /* Free buffer */
            fragment_free_reassembly_buffer(manager, buffer);

            timeout_count++;
        }
    }

    return timeout_count;
}

/**
 * \brief           Get number of active reassembly buffers
 */
size_t
xgl_fragment_get_reassembly_count(const xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return 0;
    }

    return xgct_list_count(&manager->reassembly_list);
}

/**
 * \brief           Clear all reassembly buffers
 */
void xgl_fragment_clear_reassembly(xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    /* Remove and free all reassembly buffers */
    xgct_list_node_t* node;
    while ((node = xgct_list_remove_head(&manager->reassembly_list)) != NULL) {
        xgl_reassembly_buffer_t* buffer =
            /* Intrusive node membership is established by the owning list. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
        fragment_free_reassembly_buffer(manager, buffer);
    }
}

size_t xgl_fragment_clear_reassembly_scope(xgl_fragment_manager_t* manager,
                                           uint16_t source_id,
                                           uint32_t connection_id,
                                           uint32_t session_epoch) {
    if (manager == NULL) {
        return 0U;
    }

    size_t cleared = 0U;
    xgct_list_node_t* node;
    xgct_list_node_t* tmp;
    XGCT_LIST_FOR_EACH_SAFE(&manager->reassembly_list, node, tmp) {
        xgl_reassembly_buffer_t* buffer =
            /* Intrusive node membership is established by the owning list. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        bool matches_production_scope =
            buffer->source_id == source_id &&
            buffer->connection_id == connection_id &&
            buffer->session_epoch == session_epoch;

        if (matches_production_scope) {
            xgct_list_remove(&manager->reassembly_list, node);
            fragment_free_reassembly_buffer(manager, buffer);
            cleared++;
        }
    }

    return cleared;
}

void xgl_fragment_release_message(xgl_fragment_manager_t* manager,
                                  xgl_fragment_message_t* message) {
    if (manager == NULL || message == NULL || message->data == NULL) {
        return;
    }
    manager->current_reassembly_bytes -= message->len;
    xgm_free(manager->data_allocator, message->data);
    message->data = NULL;
    message->len = 0U;
}
