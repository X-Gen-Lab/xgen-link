/**
 * \file            xgl_fragment_reassembly.c
 * \brief           Reassembly storage and fragment admission
 */

#include "xgl_fragment_internal.h"
#include <stdint.h>
#include <string.h>
#include <xgen/memory/allocator.h>

void fragment_free_reassembly_buffer(xgl_fragment_manager_t* manager,
                                     xgl_reassembly_buffer_t* buffer) {
    if (buffer == NULL) {
        return;
    }

    /* Owning queue traversals always supply their live manager. */
    if (buffer->reserved_size <= manager->current_reassembly_bytes) {
        manager->current_reassembly_bytes -= buffer->reserved_size;
    }

    if (buffer->data != NULL) {
        xgm_free(manager->data_allocator, buffer->data);
        buffer->data = NULL;
    }

    xgm_free(manager->allocator, buffer);
}

static xgl_reassembly_buffer_t*
fragment_find_reassembly_buffer(const xgl_fragment_manager_t* manager,
                                uint16_t source_id, uint32_t connection_id,
                                uint32_t session_epoch, uint32_t message_id) {
    if (manager == NULL) {
        return NULL;
    }

    xgct_list_node_t* node;
    XGCT_LIST_FOR_EACH(&manager->reassembly_list, node) {
        xgl_reassembly_buffer_t* buffer =
            /* Intrusive node membership is established by the owning list. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        if (buffer->source_id == source_id &&
            buffer->connection_id == connection_id &&
            buffer->session_epoch == session_epoch &&
            buffer->message_id == message_id) {
            return buffer;
        }
    }

    return NULL;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
static xgl_error_t fragment_create_reassembly_buffer(
    xgl_fragment_manager_t* manager, uint16_t source_id, uint32_t connection_id,
    uint32_t session_epoch, uint8_t data_type, uint32_t message_id,
    uint32_t message_len, xgl_reassembly_buffer_t** buffer_out) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (manager == NULL || buffer_out == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *buffer_out = NULL;

    if (xgct_list_count(&manager->reassembly_list) >=
        manager->max_reassembly_buffers) {
        return XGL_ERR_NO_MEMORY;
    }

    if (manager->max_message_size != 0U &&
        (size_t)message_len > manager->max_message_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (manager->max_reassembly_bytes != 0U &&
        (manager->current_reassembly_bytes > manager->max_reassembly_bytes ||
         (size_t)message_len > manager->max_reassembly_bytes -
                                   manager->current_reassembly_bytes)) {
        return XGL_ERR_NO_MEMORY;
    }

    xgl_reassembly_buffer_t* buffer = (xgl_reassembly_buffer_t*)xgm_alloc(
        manager->allocator, sizeof(xgl_reassembly_buffer_t));
    if (buffer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->source_id = source_id;
    buffer->connection_id = connection_id;
    buffer->session_epoch = session_epoch;
    buffer->message_id = message_id;
    buffer->data_type = data_type;
    buffer->timeout_ms = manager->reassembly_timeout_ms;
    buffer->buffer_size = message_len;
    buffer->reserved_size = message_len;
    buffer->data_len = message_len;

    buffer->data =
        (uint8_t*)xgm_alloc(manager->data_allocator, buffer->buffer_size);
    if (buffer->data == NULL) {
        xgm_free(manager->allocator, buffer);
        return XGL_ERR_NO_MEMORY;
    }

    manager->current_reassembly_bytes += buffer->reserved_size;

    xgct_list_node_init(&buffer->node);
    xgct_list_insert_tail(&manager->reassembly_list, &buffer->node);

    *buffer_out = buffer;
    return XGL_OK;
}

static void fragment_complete_reassembly(xgl_fragment_manager_t* manager,
                                         xgl_reassembly_buffer_t* buffer,
                                         xgl_fragment_message_t* complete) {
    complete->data = buffer->data;
    complete->len = buffer->data_len;
    xgct_list_remove(&manager->reassembly_list, &buffer->node);
    /* The payload and its budget move together; only the slot is released. */
    xgm_free(manager->allocator, buffer);
}

static xgl_error_t fragment_validate_ext_input(uint32_t fragment_offset,
                                               uint32_t message_len,
                                               size_t fragment_payload_len) {
    if (message_len == 0U || fragment_offset > message_len ||
        fragment_payload_len > (size_t)message_len - (size_t)fragment_offset) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_fragment_process_ext(
    xgl_fragment_manager_t* manager, uint16_t source_id, uint32_t connection_id,
    uint32_t session_epoch, uint8_t data_type, uint32_t message_id,
    uint32_t fragment_offset, uint32_t message_len,
    const uint8_t* fragment_payload, size_t fragment_payload_len,
    xgl_fragment_message_t* complete, uint32_t current_time_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (manager == NULL || fragment_payload == NULL ||
        fragment_payload_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (complete == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    complete->data = NULL;
    complete->len = 0U;

    xgl_error_t err = fragment_validate_ext_input(fragment_offset, message_len,
                                                  fragment_payload_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_reassembly_buffer_t* buffer = fragment_find_reassembly_buffer(
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
        buffer->buffer_size != (size_t)message_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t start = fragment_offset;
    size_t end = start + fragment_payload_len;
    xgl_error_t range_err = fragment_insert_received_range(buffer, start, end);
    if (range_err == XGL_ERR_BUSY) {
        /* Full coverage is admissible only when the owned bytes agree. */
        return memcmp(buffer->data + start, fragment_payload,
                      fragment_payload_len) == 0
                   ? XGL_ERR_BUSY
                   : XGL_ERR_INVALID_FRAME;
    }
    if (range_err != XGL_OK) {
        return range_err;
    }

    bool is_first_received_range = (buffer->received_bytes == 0U);

    memcpy(&buffer->data[start], fragment_payload, fragment_payload_len);
    buffer->received_bytes += fragment_payload_len;

    if (is_first_received_range) {
        buffer->first_fragment_time = current_time_ms;
    }

    if (buffer->received_bytes == buffer->buffer_size) {
        fragment_complete_reassembly(manager, buffer, complete);
        return XGL_OK;
    }

    return XGL_ERR_BUSY;
}
