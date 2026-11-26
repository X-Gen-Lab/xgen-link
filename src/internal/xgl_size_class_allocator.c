/**
 * \file            xgl_size_class_allocator.c
 * \brief           Fixed pool selection for instance initialization
 */

#include "internal/xgl_size_class_allocator.h"

bool xgl_size_class_measure(const xgl_size_class_spec_t* specs, size_t count,
                            size_t* storage_size) {
    size_t total = 0U;
    for (size_t i = 0U; i < count; ++i) {
        size_t block_size;
        size_t bytes;
        if (!xgl_pool_measure(specs[i].block_size, specs[i].block_count,
                              &block_size, &bytes) ||
            bytes > SIZE_MAX - total) {
            return false;
        }
        total += bytes;
    }
    *storage_size = total;
    return true;
}

static void* size_class_alloc(void* ctx, size_t size) {
    xgl_size_class_allocator_t* state = ctx;
    size_t smallest = SIZE_MAX;
    for (size_t i = 0U; i < state->pool_count; ++i) {
        const xgl_pool_t* pool = &state->pools[i];
        if (pool->block_size >= size && pool->block_size < smallest) {
            smallest = pool->block_size;
        }
    }

    /* Equal-size classes share capacity. Exhaustion never borrows a larger
     * class, whose slots may be needed by subsequent initialization work. */
    if (smallest != SIZE_MAX) {
        for (size_t i = 0U; i < state->pool_count; ++i) {
            xgl_pool_t* pool = &state->pools[i];
            if (pool->block_size == smallest && pool->free_list != NULL) {
                return xgl_pool_alloc(pool);
            }
        }
    }
    return NULL;
}

/* Fixed allocator callback parameter order. */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void size_class_free(void* ctx, void* ptr) {
    xgl_size_class_allocator_t* state = ctx;
    for (size_t i = 0U; i < state->pool_count; ++i) {
        if (xgl_pool_release(&state->pools[i], ptr)) {
            return;
        }
    }
}

bool xgl_size_class_init(xgl_size_class_allocator_t* state, xgl_pool_t* pools,
                         const xgl_size_class_spec_t* specs, size_t count,
                         void* storage, size_t storage_size) {
    size_t needed;
    if (!xgl_size_class_measure(specs, count, &needed) ||
        needed > storage_size) {
        return false;
    }
    uint8_t* cursor = storage;
    for (size_t i = 0U; i < count; ++i) {
        size_t block_size;
        size_t bytes;
        if (!xgl_pool_measure(specs[i].block_size, specs[i].block_count,
                              &block_size, &bytes)) {
            return false;
        }
        if (bytes == 0U) {
            pools[i] = (xgl_pool_t){0};
        } else {
            if (!xgl_pool_init(&pools[i], cursor, bytes, block_size,
                               specs[i].block_count)) {
                return false;
            }
            cursor += bytes;
        }
    }

    *state = (xgl_size_class_allocator_t){
        {state, size_class_alloc, size_class_free}, pools, count};
    return true;
}
