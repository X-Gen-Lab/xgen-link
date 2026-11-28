/**
 * \file            xgl_pool.h
 * \brief           Fixed slots in an instance-owned storage partition
 * \author          X-Gen Lab
 */

#ifndef XGL_POOL_H
#define XGL_POOL_H

#include "internal/xgl_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* storage;
    void* free_list;
    size_t block_size;
    size_t block_count;
    size_t free_count;
} xgl_pool_t;

/** \brief           Measure aligned stride and total partition bytes. */
bool xgl_pool_measure(size_t size, size_t count, size_t* block_size,
                      size_t* storage_size);

/** \brief           Link exactly count validated blocks into the free list. */
bool xgl_pool_init(xgl_pool_t* pool, void* storage, size_t storage_size,
                   size_t block_size, size_t count);

void* xgl_pool_alloc(xgl_pool_t* pool);

/** \brief           Release a live block; reject foreign or free blocks. */
bool xgl_pool_release(xgl_pool_t* pool, void* ptr);

/** \brief           Bind the allocation service to a runtime resource pool. */
xgl_allocator_t xgl_pool_allocator(xgl_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* XGL_POOL_H */
