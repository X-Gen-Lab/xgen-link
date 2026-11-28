/**
 * \file            xgl_pool.c
 * \brief           Fixed slots in an instance-owned storage partition
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-11-26
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "internal/xgl_pool.h"

#include <string.h>

/* Size inputs and output arguments retain the same order as workspace plans. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
bool xgl_pool_measure(size_t size, size_t count, size_t* block_size,
                      size_t* storage_size) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (count == 0U) {
        *block_size = 0U;
        *storage_size = 0U;
        return true;
    }
    if (size == 0U) {
        return false;
    }
    if (size < sizeof(void*)) {
        size = sizeof(void*);
    }
    const size_t alignment = _Alignof(xgl_max_align_t);
    const size_t remainder = size % alignment;
    const size_t padding = remainder == 0U ? 0U : alignment - remainder;
    if (size > SIZE_MAX - padding) {
        return false;
    }
    const size_t stride = size + padding;
    if (count > SIZE_MAX / stride) {
        return false;
    }
    *block_size = stride;
    *storage_size = stride * count;
    return true;
}

/* The bytes of a free block store its successor. */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void store_next(void* block, void* next) {
    memcpy(block, (const void*)&next, sizeof(next));
}

static void* load_next(const void* block) {
    void* next;
    memcpy((void*)&next, block, sizeof(next));
    return next;
}

/* Storage capacity, stride, and count are distinct parts of the plan. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
bool xgl_pool_init(xgl_pool_t* pool, void* storage, size_t storage_size,
                   size_t block_size, size_t count) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    const size_t alignment = _Alignof(xgl_max_align_t);
    if (storage == NULL || (uintptr_t)storage % alignment != 0U ||
        block_size < sizeof(void*) || block_size % alignment != 0U ||
        count == 0U || count > storage_size / block_size) {
        return false;
    }

    *pool = (xgl_pool_t){storage, NULL, block_size, count, count};
    for (size_t i = 0U; i < count; ++i) {
        void* block = pool->storage + i * block_size;
        store_next(block, pool->free_list);
        pool->free_list = block;
    }
    return true;
}

void* xgl_pool_alloc(xgl_pool_t* pool) {
    void* block = pool->free_list;
    if (block != NULL) {
        pool->free_list = load_next(block);
        --pool->free_count;
    }
    return block;
}

bool xgl_pool_release(xgl_pool_t* pool, void* ptr) {
    if (ptr == NULL || pool->storage == NULL ||
        pool->free_count >= pool->block_count) {
        return false;
    }
    const uintptr_t address = (uintptr_t)ptr;
    const uintptr_t start = (uintptr_t)pool->storage;
    if (address < start) {
        return false;
    }
    const uintptr_t offset = address - start;
    if (offset >= pool->block_count * pool->block_size ||
        offset % pool->block_size != 0U) {
        return false;
    }

    void* block = pool->free_list;
    for (size_t i = 0U; block != NULL && i < pool->free_count; ++i) {
        if (block == ptr) {
            return false;
        }
        block = load_next(block);
    }
    store_next(ptr, pool->free_list);
    pool->free_list = ptr;
    ++pool->free_count;
    return true;
}

static void* pool_service_alloc(void* ctx, size_t size) {
    xgl_pool_t* pool = ctx;
    return size <= pool->block_size ? xgl_pool_alloc(pool) : NULL;
}

/* Fixed allocator callback parameter order. */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void pool_service_free(void* ctx, void* ptr) {
    (void)xgl_pool_release(ctx, ptr);
}

xgl_allocator_t xgl_pool_allocator(xgl_pool_t* pool) {
    if (pool->storage == NULL) {
        return (xgl_allocator_t){0};
    }
    return (xgl_allocator_t){pool, pool_service_alloc, pool_service_free};
}
