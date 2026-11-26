/**
 * \file            xgl_allocator.h
 * \brief           Dispatch through an explicit XGL allocation service
 */

#ifndef XGL_ALLOCATOR_H
#define XGL_ALLOCATOR_H

#include <xgl/xgl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool xgl_allocator_is_valid(const xgl_allocator_t* allocator);
void* xgl_alloc(const xgl_allocator_t* allocator, size_t size);
void xgl_free(const xgl_allocator_t* allocator, void* ptr);

#if XGL_ALLOW_FALLBACK_MALLOC
const xgl_allocator_t* xgl_allocator_libc(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_ALLOCATOR_H */
