/**
 * \file            xgl_fragment_internal.h
 * \brief           Private fragment reassembly helpers
 */

#ifndef XGL_FRAGMENT_INTERNAL_H
#define XGL_FRAGMENT_INTERNAL_H

#include <xgl/internal/xgl_fragment.h>

void* fragment_malloc(xgl_allocator_t* allocator, size_t size);
void fragment_free(xgl_allocator_t* allocator, void* ptr);

xgl_reassembly_buffer_t* fragment_find_reassembly_buffer(
    const xgl_fragment_manager_t* manager,
    uint16_t source_id,
    uint32_t connection_id,
    uint32_t session_epoch,
    uint32_t message_id);

xgl_error_t fragment_create_reassembly_buffer(xgl_fragment_manager_t* manager,
                                               uint16_t source_id,
                                               uint32_t connection_id,
                                               uint32_t session_epoch,
                                               uint8_t data_type,
                                               uint32_t message_id,
                                               uint32_t message_len,
                                               xgl_reassembly_buffer_t** buffer_out);

void fragment_complete_reassembly(xgl_fragment_manager_t* manager,
                                  xgl_reassembly_buffer_t* buffer,
                                  uint8_t** complete_data,
                                  size_t* complete_len);

void fragment_free_reassembly_buffer(xgl_fragment_manager_t* manager,
                                     xgl_reassembly_buffer_t* buffer);

xgl_error_t fragment_insert_received_range(xgl_reassembly_buffer_t* buffer,
                                           size_t start,
                                           size_t end);

#endif /* XGL_FRAGMENT_INTERNAL_H */
