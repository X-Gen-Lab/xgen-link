/**
 * \file            xgl_allocator_internal.h
 * \brief           Source-private allocator dispatch helpers
 * \author          X-Gen Lab
 */

#ifndef XGL_ALLOCATOR_INTERNAL_H
#define XGL_ALLOCATOR_INTERNAL_H

#include <xgl/internal/xgl_allocator.h>
#include <stdbool.h>
#include <stddef.h>

bool xgl_tracking_allocator_is_alloc_callback(
    const xgl_allocator_t* allocator);

bool xgl_tracking_allocator_is_free_callback(
    const xgl_allocator_t* allocator);

void* xgl_tracking_allocator_alloc_from_interface(
    xgl_allocator_t* allocator,
    size_t size);

void xgl_tracking_allocator_free_from_interface(
    xgl_allocator_t* allocator,
    void* ptr);

#endif /* XGL_ALLOCATOR_INTERNAL_H */
