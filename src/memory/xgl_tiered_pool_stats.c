/**
 * \file            xgl_tiered_pool_stats.c
 * \brief           Tiered memory pool statistics helpers
 * \author          X-Gen Lab
 */

#include "xgl/internal/xgl_tiered_pool.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* Tiered Pool Query Operations                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get total free memory across all pools
 */
size_t xgl_tiered_pool_get_free_memory(const xgl_tiered_pool_t* pool) {
    size_t free_memory = 0;

    if (pool == NULL) {
        return 0;
    }

    if (pool->small_count > 0) {
        free_memory += xgl_mempool_get_free_count(&pool->small_pool) *
                       XGL_TIERED_POOL_SMALL_SIZE;
    }

    if (pool->medium_count > 0) {
        free_memory += xgl_mempool_get_free_count(&pool->medium_pool) *
                       XGL_TIERED_POOL_MEDIUM_SIZE;
    }

    if (pool->large_count > 0) {
        free_memory += xgl_mempool_get_free_count(&pool->large_pool) *
                       XGL_TIERED_POOL_LARGE_SIZE;
    }

    return free_memory;
}

/**
 * \brief           Get total used memory across all pools
 */
size_t xgl_tiered_pool_get_used_memory(const xgl_tiered_pool_t* pool) {
    size_t used_memory = 0;

    if (pool == NULL) {
        return 0;
    }

    if (pool->small_count > 0) {
        used_memory += xgl_mempool_get_used_count(&pool->small_pool) *
                       XGL_TIERED_POOL_SMALL_SIZE;
    }

    if (pool->medium_count > 0) {
        used_memory += xgl_mempool_get_used_count(&pool->medium_pool) *
                       XGL_TIERED_POOL_MEDIUM_SIZE;
    }

    if (pool->large_count > 0) {
        used_memory += xgl_mempool_get_used_count(&pool->large_pool) *
                       XGL_TIERED_POOL_LARGE_SIZE;
    }

    return used_memory;
}

/**
 * \brief           Get peak memory usage across all pools
 */
size_t xgl_tiered_pool_get_peak_memory(const xgl_tiered_pool_t* pool) {
    size_t peak_memory = 0;

    if (pool == NULL) {
        return 0;
    }

    if (pool->small_count > 0) {
        peak_memory += xgl_mempool_get_peak_used(&pool->small_pool) *
                       XGL_TIERED_POOL_SMALL_SIZE;
    }

    if (pool->medium_count > 0) {
        peak_memory += xgl_mempool_get_peak_used(&pool->medium_pool) *
                       XGL_TIERED_POOL_MEDIUM_SIZE;
    }

    if (pool->large_count > 0) {
        peak_memory += xgl_mempool_get_peak_used(&pool->large_pool) *
                       XGL_TIERED_POOL_LARGE_SIZE;
    }

    return peak_memory;
}

/**
 * \brief           Reset statistics for all pools
 */
void xgl_tiered_pool_reset_stats(xgl_tiered_pool_t* pool) {
    if (pool == NULL) {
        return;
    }

    if (pool->small_count > 0) {
        xgl_mempool_reset_stats(&pool->small_pool);
    }

    if (pool->medium_count > 0) {
        xgl_mempool_reset_stats(&pool->medium_pool);
    }

    if (pool->large_count > 0) {
        xgl_mempool_reset_stats(&pool->large_pool);
    }
}
