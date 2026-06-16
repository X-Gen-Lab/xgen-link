/**
 * \file            xgl_fragment.c
 * \brief           Packet Fragmentation and Reassembly Implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_fragment.h>
#include <xgl/internal/xgl_allocator.h>
#include <xgl/internal/xgl_time.h>
#include <xgl/internal/xgl_serialize.h>
#include <string.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using the configured allocator policy
 */
static void* fragment_malloc(xgl_allocator_t* allocator, size_t size) {
    return xgl_alloc(allocator, size);
}

/**
 * \brief           Free memory using the configured allocator policy
 */
static void fragment_free(xgl_allocator_t* allocator, void* ptr) {
    xgl_free(allocator, ptr);
}

/**
 * \brief           Free reassembly buffer and its data
 */
static void free_reassembly_buffer(xgl_fragment_manager_t* manager,
                                  xgl_reassembly_buffer_t* buffer) {
    if (buffer == NULL) {
        return;
    }

    if (manager != NULL && buffer->reserved_size <= manager->current_reassembly_bytes) {
        manager->current_reassembly_bytes -= buffer->reserved_size;
    }

    /* Free data buffer */
    if (buffer->data != NULL) {
        fragment_free(manager->allocator, buffer->data);
        buffer->data = NULL;
    }

    /* Free received bitmap */
    if (buffer->received_bitmap != NULL) {
        fragment_free(manager->allocator, buffer->received_bitmap);
        buffer->received_bitmap = NULL;
    }

    /* Free buffer structure */
    fragment_free(manager->allocator, buffer);
}

static xgl_reassembly_buffer_t* find_reassembly_buffer_ext(
    // cppcheck-suppress constParameterPointer
    xgl_fragment_manager_t* manager,
    uint16_t source_id,
    uint32_t connection_id,
    uint32_t session_epoch,
    uint32_t message_id) {

    if (manager == NULL) {
        return NULL;
    }

    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&manager->reassembly_list, node) {
        xgl_reassembly_buffer_t* buffer =
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        if (buffer->source_id == source_id &&
            buffer->connection_id == connection_id &&
            buffer->session_epoch == session_epoch &&
            buffer->message_id == message_id) {
            return buffer;
        }
    }

    return NULL;
}

static bool fragment_range_overlaps(const xgl_fragment_received_range_t* range,
                                    size_t start,
                                    size_t end) {
    return range != NULL && start < range->end && end > range->start;
}

static void fragment_remove_range_at(xgl_reassembly_buffer_t* buffer,
                                     size_t index) {
    if (buffer == NULL || index >= buffer->received_range_count) {
        return;
    }

    for (size_t i = index + 1U; i < buffer->received_range_count; ++i) {
        buffer->received_ranges[i - 1U] = buffer->received_ranges[i];
    }
    buffer->received_range_count--;
}

static xgl_error_t fragment_insert_received_range(xgl_reassembly_buffer_t* buffer,
                                                  size_t start,
                                                  size_t end) {
    if (buffer == NULL || start >= end) {
        return XGL_ERR_INVALID_PARAM;
    }

    for (size_t i = 0; i < buffer->received_range_count; ++i) {
        if (fragment_range_overlaps(&buffer->received_ranges[i], start, end)) {
            return XGL_ERR_BUSY;
        }
    }

    size_t insert_index = 0U;
    while (insert_index < buffer->received_range_count &&
           buffer->received_ranges[insert_index].start < start) {
        insert_index++;
    }

    if (insert_index > 0U &&
        buffer->received_ranges[insert_index - 1U].end == start) {
        buffer->received_ranges[insert_index - 1U].end = end;
        if (insert_index < buffer->received_range_count &&
            buffer->received_ranges[insert_index].start == end) {
            buffer->received_ranges[insert_index - 1U].end =
                buffer->received_ranges[insert_index].end;
            fragment_remove_range_at(buffer, insert_index);
        }
        return XGL_OK;
    }

    if (insert_index < buffer->received_range_count &&
        buffer->received_ranges[insert_index].start == end) {
        buffer->received_ranges[insert_index].start = start;
        return XGL_OK;
    }

    if (buffer->received_range_count >= XGL_FRAGMENT_MAX_RECEIVED_RANGES) {
        return XGL_ERR_NO_MEMORY;
    }

    for (size_t i = buffer->received_range_count; i > insert_index; --i) {
        buffer->received_ranges[i] = buffer->received_ranges[i - 1U];
    }
    buffer->received_ranges[insert_index].start = start;
    buffer->received_ranges[insert_index].end = end;
    buffer->received_range_count++;

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Fragmentation Manager Functions                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize fragmentation manager
 */
xgl_error_t xgl_fragment_init(xgl_fragment_manager_t* manager,
                              size_t max_reassembly_buffers,
                              uint32_t reassembly_timeout_ms,
                              xgl_allocator_t* allocator) {
    if (manager == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Initialize reassembly list */
    xgl_list_init(&manager->reassembly_list);

    manager->next_message_id = 0;

    /* Store configuration */
    manager->max_reassembly_buffers = max_reassembly_buffers;
    manager->reassembly_timeout_ms = reassembly_timeout_ms;
    manager->allocator = allocator;
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

xgl_error_t xgl_fragment_process_ext(xgl_fragment_manager_t* manager,
                                     uint16_t source_id,
                                     uint32_t connection_id,
                                     uint32_t session_epoch,
                                     uint8_t data_type,
                                     uint32_t message_id,
                                     uint32_t fragment_offset,
                                     uint32_t message_len,
                                     const uint8_t* fragment_payload,
                                     size_t fragment_payload_len,
                                     uint8_t** complete_data,
                                     size_t* complete_len,
                                     uint32_t current_time_ms) {
    if (manager == NULL || fragment_payload == NULL || fragment_payload_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (complete_data == NULL || complete_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *complete_data = NULL;
    *complete_len = 0U;

    if (message_len == 0U ||
        fragment_offset > message_len ||
        fragment_payload_len > (size_t)message_len - (size_t)fragment_offset) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_reassembly_buffer_t* buffer = find_reassembly_buffer_ext(manager,
                                                                 source_id,
                                                                 connection_id,
                                                                 session_epoch,
                                                                 message_id);
    if (buffer == NULL) {
        if (xgl_list_count(&manager->reassembly_list) >=
            manager->max_reassembly_buffers) {
            return XGL_ERR_NO_MEMORY;
        }

        if (manager->max_message_size != 0U &&
            (size_t)message_len > manager->max_message_size) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }

        if (manager->max_reassembly_bytes != 0U &&
            (manager->current_reassembly_bytes > manager->max_reassembly_bytes ||
             (size_t)message_len > manager->max_reassembly_bytes - manager->current_reassembly_bytes)) {
            return XGL_ERR_NO_MEMORY;
        }

        buffer = (xgl_reassembly_buffer_t*)fragment_malloc(manager->allocator,
                                                           sizeof(xgl_reassembly_buffer_t));
        if (buffer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }

        memset(buffer, 0, sizeof(xgl_reassembly_buffer_t));
        buffer->source_id = source_id;
        buffer->connection_id = connection_id;
        buffer->session_epoch = session_epoch;
        buffer->message_id = message_id;
        buffer->data_type = data_type;
        buffer->timeout_ms = manager->reassembly_timeout_ms;
        buffer->buffer_size = message_len;
        buffer->reserved_size = message_len;
        buffer->data_len = message_len;

        buffer->received_bitmap = NULL;
        buffer->received_range_count = 0U;

        buffer->data = (uint8_t*)fragment_malloc(manager->allocator,
                                                 buffer->buffer_size);
        if (buffer->data == NULL) {
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }

        manager->current_reassembly_bytes += buffer->reserved_size;

        xgl_list_node_init(&buffer->node);
        xgl_list_insert_tail(&manager->reassembly_list, &buffer->node);
    }

    if (buffer->data_type != data_type ||
        buffer->buffer_size != (size_t)message_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t start = fragment_offset;
    size_t end = start + fragment_payload_len;
    xgl_error_t range_err = fragment_insert_received_range(buffer, start, end);
    if (range_err != XGL_OK) {
        return range_err;
    }

    bool is_first_received_range = (buffer->received_bytes == 0U);

    memcpy(&buffer->data[start], fragment_payload, fragment_payload_len);
    buffer->received_bytes += fragment_payload_len;

    if (is_first_received_range) {
        buffer->first_fragment_time = (current_time_ms == 0U) ?
                                      xgl_time_ms() : current_time_ms;
    }

    if (buffer->received_bytes == buffer->buffer_size) {
        *complete_data = buffer->data;
        *complete_len = buffer->data_len;

        xgl_list_remove(&manager->reassembly_list, &buffer->node);

        if (buffer->received_bitmap != NULL) {
            fragment_free(manager->allocator, buffer->received_bitmap);
        }
        if (buffer->reserved_size <= manager->current_reassembly_bytes) {
            manager->current_reassembly_bytes -= buffer->reserved_size;
        }
        fragment_free(manager->allocator, buffer);

        return XGL_OK;
    }

    return XGL_ERR_BUSY;
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
    xgl_list_node_t* node;
    xgl_list_node_t* tmp;
    XGL_LIST_FOR_EACH_SAFE(&manager->reassembly_list, node, tmp) {
        xgl_reassembly_buffer_t* buffer =
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        /* Skip if first fragment hasn't been received yet */
        if (buffer->first_fragment_time == 0) {
            continue;
        }

        /* Calculate elapsed time */
        uint32_t elapsed_ms = current_time_ms - buffer->first_fragment_time;

        /* Check if timeout occurred */
        if (elapsed_ms >= buffer->timeout_ms) {
            /* Remove from list */
            xgl_list_remove(&manager->reassembly_list, node);

            /* Free buffer */
            free_reassembly_buffer(manager, buffer);

            timeout_count++;
        }
    }

    return timeout_count;
}

/**
 * \brief           Get number of active reassembly buffers
 */
size_t xgl_fragment_get_reassembly_count(const xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return 0;
    }

    return xgl_list_count(&manager->reassembly_list);
}

/**
 * \brief           Clear all reassembly buffers
 */
void xgl_fragment_clear_reassembly(xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    /* Remove and free all reassembly buffers */
    xgl_list_node_t* node;
    while ((node = xgl_list_remove_head(&manager->reassembly_list)) != NULL) {
        xgl_reassembly_buffer_t* buffer =
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
        free_reassembly_buffer(manager, buffer);
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
    xgl_list_node_t* node;
    xgl_list_node_t* tmp;
    XGL_LIST_FOR_EACH_SAFE(&manager->reassembly_list, node, tmp) {
        xgl_reassembly_buffer_t* buffer =
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        bool matches_production_scope =
            buffer->source_id == source_id &&
            buffer->connection_id == connection_id &&
            buffer->session_epoch == session_epoch;

        if (matches_production_scope) {
            xgl_list_remove(&manager->reassembly_list, node);
            free_reassembly_buffer(manager, buffer);
            cleared++;
        }
    }

    return cleared;
}

void xgl_fragment_free_data(xgl_fragment_manager_t* manager,
                            uint8_t* data) {
    if (manager == NULL || data == NULL) {
        return;
    }

    fragment_free(manager->allocator, data);
}
