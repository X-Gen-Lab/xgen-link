/**
 * \file            xgl_mempool_ts.c
 * \brief           Thread-safe fixed-size memory pool wrappers
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_mempool.h>
#include <stddef.h>

#ifdef XGL_THREAD_SAFE

#include <xgl/internal/xgl_mutex.h>

/**
 * \brief           Initialize thread-safe memory pool
 */
int xgl_mempool_ts_init(xgl_mempool_ts_t* pool, void* buffer,
                        size_t buffer_size, size_t block_size) {
    int result;

    if (pool == NULL) {
        return -1;
    }

    result = xgl_mempool_init(&pool->pool, buffer, buffer_size, block_size);
    if (result != 0) {
        return result;
    }

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
