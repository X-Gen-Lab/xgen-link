/**
 * \file            xgl_fragment.c
 * \brief           Packet Fragmentation and Reassembly Implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_fragment.h>
#include <xgl/internal/xgl_serialize.h>
#include <xgl/internal/xgl_time.h>

#include <stdint.h>
#include <string.h>

#include "xgl_fragment_internal.h"

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

static xgl_error_t fragment_validate_ext_input(uint32_t fragment_offset,
                                               uint32_t message_len,
                                               size_t fragment_payload_len)
{
    if (message_len == 0U || fragment_offset > message_len ||
        fragment_payload_len >
            (size_t) message_len - (size_t) fragment_offset) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Fragmentation Manager Functions                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize fragmentation manager
 */
xgl_error_t xgl_fragment_init(xgl_fragment_manager_t *manager,
                              size_t max_reassembly_buffers,
                              uint32_t reassembly_timeout_ms,
                              xgl_allocator_t *allocator)
{
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

xgl_error_t xgl_fragment_set_limits(xgl_fragment_manager_t *manager,
                                    size_t max_message_size,
                                    size_t max_reassembly_bytes)
{
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
void xgl_fragment_destroy(xgl_fragment_manager_t *manager)
{
    if (manager == NULL) {
        return;
    }

    /* Clear all reassembly buffers */
    xgl_fragment_clear_reassembly(manager);
}

xgl_error_t xgl_fragment_process_ext(
    xgl_fragment_manager_t *manager, uint16_t source_id, uint32_t connection_id,
    uint32_t session_epoch, uint8_t data_type, uint32_t message_id,
    uint32_t fragment_offset, uint32_t message_len,
    const uint8_t *fragment_payload, size_t fragment_payload_len,
    uint8_t **complete_data, size_t *complete_len, uint32_t current_time_ms)
{
    if (manager == NULL || fragment_payload == NULL ||
        fragment_payload_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (complete_data == NULL || complete_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *complete_data = NULL;
    *complete_len = 0U;

    xgl_error_t err = fragment_validate_ext_input(fragment_offset, message_len,
                                                  fragment_payload_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_reassembly_buffer_t *buffer = fragment_find_reassembly_buffer(
        manager, source_id, connection_id, session_epoch, message_id);
    if (buffer == NULL) {
        err = fragment_create_reassembly_buffer(
            manager, source_id, connection_id, session_epoch, data_type,
            message_id, message_len, &buffer);
        if (err != XGL_OK) {
            return err;
        }
    }

    if (buffer->data_type != data_type ||
        buffer->buffer_size != (size_t) message_len) {
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
        buffer->first_fragment_time =
            (current_time_ms == 0U) ? xgl_time_ms() : current_time_ms;
    }

    if (buffer->received_bytes == buffer->buffer_size) {
        fragment_complete_reassembly(manager, buffer, complete_data,
                                     complete_len);
        return XGL_OK;
    }

    return XGL_ERR_BUSY;
}
