/**
 * \file            xgl_reliable_internal.h
 * \brief           Reliable queue private helpers
 * \author          X-Gen Lab
 */

#ifndef XGL_RELIABLE_INTERNAL_H
#define XGL_RELIABLE_INTERNAL_H

#include <xgl/internal/xgl_reliable.h>

void* reliable_malloc(xgl_allocator_t* allocator, size_t size);
void reliable_free(xgl_allocator_t* allocator, void* ptr);

void reliable_index_packet(xgl_reliable_queue_t* queue,
                           xgl_reliable_packet_t* packet);

void reliable_unindex_packet(xgl_reliable_queue_t* queue,
                             xgl_reliable_packet_t* packet);

void reliable_free_packet(xgl_reliable_queue_t* queue,
                          xgl_reliable_packet_t* packet);

#endif /* XGL_RELIABLE_INTERNAL_H */
