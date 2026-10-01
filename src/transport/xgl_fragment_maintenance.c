/**
 * \file            xgl_fragment_maintenance.c
 * \brief           Fragment reassembly maintenance helpers
 */

#include "xgl_fragment_internal.h"

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

void xgl_fragment_free_data(const xgl_fragment_manager_t* manager,
                            uint8_t* data) {
    if (manager == NULL || data == NULL) {
        return;
    }

    xgm_free(manager->data_allocator, data);
}
