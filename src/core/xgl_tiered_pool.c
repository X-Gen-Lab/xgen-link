/**
 * \file            xgl_tiered_pool.c
 * \brief           Tiered memory pool implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_tiered_pool.h"
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Tiered Pool Initialization                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize tiered memory pool
 * \details         Allocates buffers and initializes three memory pools
 */
int xgl_tiered_pool_init(xgl_tiered_pool_t* pool, size_t small_count,
                         size_t medium_count, size_t large_count) {
    if (pool == NULL) {
        return -1;
    }
    
    /* Initialize structure */
    memset(pool, 0, sizeof(xgl_tiered_pool_t));
    
    pool->small_count = small_count;
    pool->medium_count = medium_count;
    pool->large_count = large_count;
    
    /* Allocate small pool buffer */
    if (small_count > 0) {
        size_t small_buffer_size = small_count * XGL_TIERED_POOL_SMALL_SIZE;
        pool->small_buffer = (uint8_t*)malloc(small_buffer_size);
        if (pool->small_buffer == NULL) {
            goto error_cleanup;
        }
        
        if (xgl_mempool_init(&pool->small_pool, pool->small_buffer,
                            small_buffer_size,
                            XGL_TIERED_POOL_SMALL_SIZE) != 0) {
            goto error_cleanup;
        }
    }
    
    /* Allocate medium pool buffer */
    if (medium_count > 0) {
        size_t medium_buffer_size = medium_count * XGL_TIERED_POOL_MEDIUM_SIZE;
        pool->medium_buffer = (uint8_t*)malloc(medium_buffer_size);
        if (pool->medium_buffer == NULL) {
            goto error_cleanup;
        }
        
        if (xgl_mempool_init(&pool->medium_pool, pool->medium_buffer,
                            medium_buffer_size,
                            XGL_TIERED_POOL_MEDIUM_SIZE) != 0) {
            goto error_cleanup;
        }
    }
    
    /* Allocate large pool buffer */
    if (large_count > 0) {
        size_t large_buffer_size = large_count * XGL_TIERED_POOL_LARGE_SIZE;
        pool->large_buffer = (uint8_t*)malloc(large_buffer_size);
        if (pool->large_buffer == NULL) {
            goto error_cleanup;
        }
        
        if (xgl_mempool_init(&pool->large_pool, pool->large_buffer,
                            large_buffer_size,
                            XGL_TIERED_POOL_LARGE_SIZE) != 0) {
            goto error_cleanup;
        }
    }
    
    return 0;
    
error_cleanup:
    xgl_tiered_pool_destroy(pool);
    return -1;
}

/**
 * \brief           Destroy tiered memory pool
 * \details         Frees all allocated buffers and destroys pools
 */
void xgl_tiered_pool_destroy(xgl_tiered_pool_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    /* Destroy pools */
    if (pool->small_count > 0) {
        xgl_mempool_destroy(&pool->small_pool);
    }
    if (pool->medium_count > 0) {
        xgl_mempool_destroy(&pool->medium_pool);
    }
    if (pool->large_count > 0) {
        xgl_mempool_destroy(&pool->large_pool);
    }
    
    /* Free buffers */
    if (pool->small_buffer != NULL) {
        free(pool->small_buffer);
        pool->small_buffer = NULL;
    }
    if (pool->medium_buffer != NULL) {
        free(pool->medium_buffer);
        pool->medium_buffer = NULL;
    }
    if (pool->large_buffer != NULL) {
        free(pool->large_buffer);
        pool->large_buffer = NULL;
    }
    
    /* Clear structure */
    memset(pool, 0, sizeof(xgl_tiered_pool_t));
}

/*---------------------------------------------------------------------------*/
/* Tiered Pool Operations                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Smart allocation (selects appropriate pool)
 * \details         Selects the smallest pool that can fit the requested size
 */
void* xgl_tiered_pool_alloc(xgl_tiered_pool_t* pool, size_t size) {
    void* ptr = NULL;
    
    if (pool == NULL || size == 0) {
        return NULL;
    }
    
    /* Try small pool first if size fits */
    if (size <= XGL_TIERED_POOL_SMALL_SIZE && pool->small_count > 0) {
        ptr = xgl_mempool_alloc(&pool->small_pool);
        if (ptr != NULL) {
            return ptr;
        }
    }
    
    /* Try medium pool if size fits */
    if (size <= XGL_TIERED_POOL_MEDIUM_SIZE && pool->medium_count > 0) {
        ptr = xgl_mempool_alloc(&pool->medium_pool);
        if (ptr != NULL) {
            return ptr;
        }
    }
    
    /* Try large pool if size fits */
    if (size <= XGL_TIERED_POOL_LARGE_SIZE && pool->large_count > 0) {
        ptr = xgl_mempool_alloc(&pool->large_pool);
        if (ptr != NULL) {
            return ptr;
        }
    }
    
    /* All pools exhausted or size too large */
    return NULL;
}

/**
 * \brief           Free memory back to tiered pool
 * \details         Returns memory to the appropriate pool based on size
 */
void xgl_tiered_pool_free(xgl_tiered_pool_t* pool, void* ptr, size_t size) {
    if (pool == NULL || ptr == NULL || size == 0) {
        return;
    }
    
    /* Determine which pool the memory belongs to based on size */
    if (size <= XGL_TIERED_POOL_SMALL_SIZE && pool->small_count > 0) {
        xgl_mempool_free(&pool->small_pool, ptr);
    } else if (size <= XGL_TIERED_POOL_MEDIUM_SIZE && pool->medium_count > 0) {
        xgl_mempool_free(&pool->medium_pool, ptr);
    } else if (size <= XGL_TIERED_POOL_LARGE_SIZE && pool->large_count > 0) {
        xgl_mempool_free(&pool->large_pool, ptr);
    }
    /* If size doesn't match any pool, ignore (shouldn't happen in normal use) */
}

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
