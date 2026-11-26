/**
 * \file            xgl_size_class_allocator.h
 * \brief           Fixed pool selection for instance initialization
 */

#ifndef XGL_SIZE_CLASS_ALLOCATOR_H
#define XGL_SIZE_CLASS_ALLOCATOR_H

#include "internal/xgl_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t block_size;
    size_t block_count;
} xgl_size_class_spec_t;

typedef struct {
    xgl_allocator_t service;
    xgl_pool_t* pools;
    size_t pool_count;
} xgl_size_class_allocator_t;

bool xgl_size_class_measure(const xgl_size_class_spec_t* specs, size_t count,
                            size_t* storage_size);

/** \brief           Bind one pool per class in a caller-owned partition. */
bool xgl_size_class_init(xgl_size_class_allocator_t* state, xgl_pool_t* pools,
                         const xgl_size_class_spec_t* specs, size_t count,
                         void* storage, size_t storage_size);

#ifdef __cplusplus
}
#endif

#endif /* XGL_SIZE_CLASS_ALLOCATOR_H */
