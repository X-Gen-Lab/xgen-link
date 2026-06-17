/**
 * \file            xgl_fragment_reassembly.c
 * \brief           Fragment reassembly buffer management
 * \author          X-Gen Lab
 */

#include "xgl_fragment_internal.h"
#include <xgl/internal/xgl_allocator.h>
#include <string.h>

void* fragment_malloc(xgl_allocator_t* allocator, size_t size) {
    return xgl_alloc(allocator, size);
}

void fragment_free(xgl_allocator_t* allocator, void* ptr) {
    xgl_free(allocator, ptr);
}

void fragment_free_reassembly_buffer(xgl_fragment_manager_t* manager,
                                     xgl_reassembly_buffer_t* buffer) {
    if (buffer == NULL) {
        return;
    }

    if (manager != NULL && buffer->reserved_size <= manager->current_reassembly_bytes) {
        manager->current_reassembly_bytes -= buffer->reserved_size;
    }

    if (buffer->data != NULL) {
        fragment_free(manager->allocator, buffer->data);
        buffer->data = NULL;
    }

    if (buffer->received_bitmap != NULL) {
        fragment_free(manager->allocator, buffer->received_bitmap);
        buffer->received_bitmap = NULL;
    }

    fragment_free(manager->allocator, buffer);
}

xgl_reassembly_buffer_t* fragment_find_reassembly_buffer(
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

xgl_error_t fragment_create_reassembly_buffer(xgl_fragment_manager_t* manager,
                                               uint16_t source_id,
                                               uint32_t connection_id,
                                               uint32_t session_epoch,
                                               uint8_t data_type,
                                               uint32_t message_id,
                                               uint32_t message_len,
                                               xgl_reassembly_buffer_t** buffer_out) {
    if (manager == NULL || buffer_out == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *buffer_out = NULL;

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

    xgl_reassembly_buffer_t* buffer =
        (xgl_reassembly_buffer_t*)fragment_malloc(manager->allocator,
                                                  sizeof(xgl_reassembly_buffer_t));
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

    buffer->data = (uint8_t*)fragment_malloc(manager->allocator,
                                             buffer->buffer_size);
    if (buffer->data == NULL) {
        fragment_free(manager->allocator, buffer);
        return XGL_ERR_NO_MEMORY;
    }

    manager->current_reassembly_bytes += buffer->reserved_size;

    xgl_list_node_init(&buffer->node);
    xgl_list_insert_tail(&manager->reassembly_list, &buffer->node);

    *buffer_out = buffer;
    return XGL_OK;
}

void fragment_complete_reassembly(xgl_fragment_manager_t* manager,
                                  xgl_reassembly_buffer_t* buffer,
                                  uint8_t** complete_data,
                                  size_t* complete_len) {
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
}
