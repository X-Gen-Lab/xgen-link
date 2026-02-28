/**
 * \file            xgl_mempool.c
 * \brief           Fixed-size block memory pool implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_mempool.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Memory Pool Initialization                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize memory pool
 * \details         Divides the provided buffer into fixed-size blocks and
 *                  creates a free list using the first pointer-sized bytes
 *                  of each block
 */
int xgl_mempool_init(xgl_mempool_t* pool, void* buffer, size_t buffer_size,
                     size_t block_size) {
    size_t i;
    uint8_t* block_ptr;
    void** next_ptr;
    
    /* Validate parameters */
    if (pool == NULL || buffer == NULL || buffer_size == 0 || block_size == 0) {
        return -1;
    }
    
    /* Block size must be at least pointer size for free list */
    if (block_size < sizeof(void*)) {
        return -1;
    }
    
    /* Calculate number of blocks */
    pool->block_count = buffer_size / block_size;
    
    /* Need at least one block */
    if (pool->block_count == 0) {
        return -1;
    }
    
    /* Initialize pool structure */
    pool->pool = (uint8_t*)buffer;
    pool->block_size = block_size;
    pool->free_count = pool->block_count;
    pool->peak_used = 0;
    pool->free_list = NULL;
    
    /* Build free list by linking all blocks */
    for (i = 0; i < pool->block_count; i++) {
        block_ptr = pool->pool + (i * block_size);
        next_ptr = (void**)block_ptr;
        *next_ptr = pool->free_list;
        pool->free_list = block_ptr;
    }
    
    return 0;
}

/**
 * \brief           Destroy memory pool
 * \details         Clears the pool structure (does not free the buffer)
 */
void xgl_mempool_destroy(xgl_mempool_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    /* Clear pool structure */
    memset(pool, 0, sizeof(xgl_mempool_t));
}

/*---------------------------------------------------------------------------*/
/* Memory Pool Operations                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate a block from memory pool
 * \details         Removes the first block from the free list and returns it
 */
void* xgl_mempool_alloc(xgl_mempool_t* pool) {
    void* block;
    void** next_ptr;
    size_t used_count;
    
    /* Validate parameter */
    if (pool == NULL) {
        return NULL;
    }
    
    /* Check if pool is exhausted */
    if (pool->free_list == NULL) {
        return NULL;
    }
    
    /* Remove first block from free list */
    block = pool->free_list;
    next_ptr = (void**)block;
    pool->free_list = *next_ptr;
    
    /* Update statistics */
    pool->free_count--;
    used_count = pool->block_count - pool->free_count;
    if (used_count > pool->peak_used) {
        pool->peak_used = used_count;
    }
    
    return block;
}

/**
 * \brief           Free a block back to memory pool
 * \details         Adds the block to the head of the free list
 */
void xgl_mempool_free(xgl_mempool_t* pool, void* ptr) {
    void** next_ptr;
    
    /* Validate parameters */
    if (pool == NULL || ptr == NULL) {
        return;
    }
    
    /* Add block to head of free list */
    next_ptr = (void**)ptr;
    *next_ptr = pool->free_list;
    pool->free_list = ptr;
    
    /* Update statistics */
    pool->free_count++;
}

/*---------------------------------------------------------------------------*/
/* Memory Pool Query Operations                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get number of free blocks
 */
size_t xgl_mempool_get_free_count(const xgl_mempool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    
    return pool->free_count;
}

/**
 * \brief           Get number of used blocks
 */
size_t xgl_mempool_get_used_count(const xgl_mempool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    
    return pool->block_count - pool->free_count;
}

/**
 * \brief           Get peak number of blocks used
 */
size_t xgl_mempool_get_peak_used(const xgl_mempool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    
    return pool->peak_used;
}

/**
 * \brief           Check if memory pool is empty
 */
bool xgl_mempool_is_empty(const xgl_mempool_t* pool) {
    if (pool == NULL) {
        return true;
    }
    
    return pool->free_count == pool->block_count;
}

/**
 * \brief           Check if memory pool is full
 */
bool xgl_mempool_is_full(const xgl_mempool_t* pool) {
    if (pool == NULL) {
        return true;
    }
    
    return pool->free_count == 0;
}

/**
 * \brief           Reset pool statistics
 */
void xgl_mempool_reset_stats(xgl_mempool_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    pool->peak_used = pool->block_count - pool->free_count;
}

/*---------------------------------------------------------------------------*/
/* Thread-Safe Variants                                                      */
/*---------------------------------------------------------------------------*/

#ifdef XGL_THREAD_SAFE

#include "xgl_mutex.h"

/**
 * \brief           Initialize thread-safe memory pool
 */
int xgl_mempool_ts_init(xgl_mempool_ts_t* pool, void* buffer,
                        size_t buffer_size, size_t block_size) {
    int result;
    
    if (pool == NULL) {
        return -1;
    }
    
    /* Initialize underlying pool */
    result = xgl_mempool_init(&pool->pool, buffer, buffer_size, block_size);
    if (result != 0) {
        return result;
    }
    
    /* Initialize mutex */
    return xgl_mutex_init(&pool->mutex);
}

/**
 * \brief           Destroy thread-safe memory pool
 */
void xgl_mempool_ts_destroy(xgl_mempool_ts_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    xgl_mutex_destroy(&pool->mutex);
    xgl_mempool_destroy(&pool->pool);
}

/**
 * \brief           Allocate a block from thread-safe memory pool
 */
void* xgl_mempool_ts_alloc(xgl_mempool_ts_t* pool) {
    void* block;
    
    if (pool == NULL) {
        return NULL;
    }
    
    xgl_mutex_lock(&pool->mutex);
    block = xgl_mempool_alloc(&pool->pool);
    xgl_mutex_unlock(&pool->mutex);
    
    return block;
}

/**
 * \brief           Free a block back to thread-safe memory pool
 */
void xgl_mempool_ts_free(xgl_mempool_ts_t* pool, void* ptr) {
    if (pool == NULL || ptr == NULL) {
        return;
    }
    
    xgl_mutex_lock(&pool->mutex);
    xgl_mempool_free(&pool->pool, ptr);
    xgl_mutex_unlock(&pool->mutex);
}

/**
 * \brief           Get number of free blocks in thread-safe pool
 */
size_t xgl_mempool_ts_get_free_count(xgl_mempool_ts_t* pool) {
    size_t count;
    
    if (pool == NULL) {
        return 0;
    }
    
    xgl_mutex_lock(&pool->mutex);
    count = xgl_mempool_get_free_count(&pool->pool);
    xgl_mutex_unlock(&pool->mutex);
    
    return count;
}

/**
 * \brief           Get number of used blocks in thread-safe pool
 */
size_t xgl_mempool_ts_get_used_count(xgl_mempool_ts_t* pool) {
    size_t count;
    
    if (pool == NULL) {
        return 0;
    }
    
    xgl_mutex_lock(&pool->mutex);
    count = xgl_mempool_get_used_count(&pool->pool);
    xgl_mutex_unlock(&pool->mutex);
    
    return count;
}

/**
 * \brief           Get peak number of blocks used in thread-safe pool
 */
size_t xgl_mempool_ts_get_peak_used(xgl_mempool_ts_t* pool) {
    size_t peak;
    
    if (pool == NULL) {
        return 0;
    }
    
    xgl_mutex_lock(&pool->mutex);
    peak = xgl_mempool_get_peak_used(&pool->pool);
    xgl_mutex_unlock(&pool->mutex);
    
    return peak;
}

/**
 * \brief           Check if thread-safe memory pool is empty
 */
bool xgl_mempool_ts_is_empty(xgl_mempool_ts_t* pool) {
    bool result;
    
    if (pool == NULL) {
        return true;
    }
    
    xgl_mutex_lock(&pool->mutex);
    result = xgl_mempool_is_empty(&pool->pool);
    xgl_mutex_unlock(&pool->mutex);
    
    return result;
}

/**
 * \brief           Check if thread-safe memory pool is full
 */
bool xgl_mempool_ts_is_full(xgl_mempool_ts_t* pool) {
    bool result;
    
    if (pool == NULL) {
        return true;
    }
    
    xgl_mutex_lock(&pool->mutex);
    result = xgl_mempool_is_full(&pool->pool);
    xgl_mutex_unlock(&pool->mutex);
    
    return result;
}

/**
 * \brief           Reset thread-safe pool statistics
 */
void xgl_mempool_ts_reset_stats(xgl_mempool_ts_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    xgl_mutex_lock(&pool->mutex);
    xgl_mempool_reset_stats(&pool->pool);
    xgl_mutex_unlock(&pool->mutex);
}

#endif /* XGL_THREAD_SAFE */
