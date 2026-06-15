/**
 * \file            xgl_mempool.h
 * \brief           Fixed-size block memory pool
 * \author          Nexus Team
 */

#ifndef XGL_MEMPOOL_H
#define XGL_MEMPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Memory Pool Structure                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Memory pool structure
 */
typedef struct {
    uint8_t* pool;                  /**< Pointer to memory pool buffer */
    size_t block_size;              /**< Size of each block in bytes */
    size_t block_count;             /**< Total number of blocks */
    size_t free_count;              /**< Number of free blocks */
    size_t peak_used;               /**< Peak number of blocks used */
    void* free_list;                /**< Pointer to first free block */
} xgl_mempool_t;

/*---------------------------------------------------------------------------*/
/* Memory Pool Initialization                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize memory pool
 * \param[in,out]   pool: Pointer to memory pool structure
 * \param[in]       buffer: Pointer to pre-allocated buffer
 * \param[in]       buffer_size: Size of buffer in bytes
 * \param[in]       block_size: Size of each block in bytes
 * \return          0 on success, -1 on error
 */
int xgl_mempool_init(xgl_mempool_t* pool, void* buffer, size_t buffer_size,
                     size_t block_size);

/**
 * \brief           Destroy memory pool
 * \param[in,out]   pool: Pointer to memory pool structure
 */
void xgl_mempool_destroy(xgl_mempool_t* pool);

/*---------------------------------------------------------------------------*/
/* Memory Pool Operations                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate a block from memory pool
 * \param[in,out]   pool: Pointer to memory pool structure
 * \return          Pointer to allocated block, NULL if pool is exhausted
 */
void* xgl_mempool_alloc(xgl_mempool_t* pool);

/**
 * \brief           Free a block back to memory pool
 * \param[in,out]   pool: Pointer to memory pool structure
 * \param[in]       ptr: Pointer to block to free
 */
void xgl_mempool_free(xgl_mempool_t* pool, void* ptr);

/*---------------------------------------------------------------------------*/
/* Memory Pool Query Operations                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get number of free blocks
 * \param[in]       pool: Pointer to memory pool structure
 * \return          Number of free blocks
 */
size_t xgl_mempool_get_free_count(const xgl_mempool_t* pool);

/**
 * \brief           Get number of used blocks
 * \param[in]       pool: Pointer to memory pool structure
 * \return          Number of used blocks
 */
size_t xgl_mempool_get_used_count(const xgl_mempool_t* pool);

/**
 * \brief           Get peak number of blocks used
 * \param[in]       pool: Pointer to memory pool structure
 * \return          Peak number of blocks used
 */
size_t xgl_mempool_get_peak_used(const xgl_mempool_t* pool);

/**
 * \brief           Check if memory pool is empty (all blocks free)
 * \param[in]       pool: Pointer to memory pool structure
 * \return          true if all blocks are free, false otherwise
 */
bool xgl_mempool_is_empty(const xgl_mempool_t* pool);

/**
 * \brief           Check if memory pool is full (no free blocks)
 * \param[in]       pool: Pointer to memory pool structure
 * \return          true if no free blocks, false otherwise
 */
bool xgl_mempool_is_full(const xgl_mempool_t* pool);

/**
 * \brief           Reset pool statistics
 * \param[in,out]   pool: Pointer to memory pool structure
 */
void xgl_mempool_reset_stats(xgl_mempool_t* pool);

/*---------------------------------------------------------------------------*/
/* Thread-Safe Variants (if XGL_THREAD_SAFE enabled)                        */
/*---------------------------------------------------------------------------*/

#ifdef XGL_THREAD_SAFE

#include "xgl/internal/xgl_mutex.h"

/**
 * \brief           Thread-safe memory pool structure
 */
typedef struct {
    xgl_mempool_t pool;             /**< Underlying memory pool */
    xgl_mutex_t mutex;              /**< Mutex for thread safety */
} xgl_mempool_ts_t;

/**
 * \brief           Initialize thread-safe memory pool
 * \param[in,out]   pool: Pointer to thread-safe memory pool structure
 * \param[in]       buffer: Pointer to pre-allocated buffer
 * \param[in]       buffer_size: Size of buffer in bytes
 * \param[in]       block_size: Size of each block in bytes
 * \return          0 on success, error code otherwise
 */
int xgl_mempool_ts_init(xgl_mempool_ts_t* pool, void* buffer,
                        size_t buffer_size, size_t block_size);

/**
 * \brief           Destroy thread-safe memory pool
 * \param[in,out]   pool: Pointer to thread-safe memory pool structure
 */
void xgl_mempool_ts_destroy(xgl_mempool_ts_t* pool);

/**
 * \brief           Allocate a block from thread-safe memory pool
 * \param[in,out]   pool: Pointer to thread-safe memory pool structure
 * \return          Pointer to allocated block, NULL if pool is exhausted
 */
void* xgl_mempool_ts_alloc(xgl_mempool_ts_t* pool);

/**
 * \brief           Free a block back to thread-safe memory pool
 * \param[in,out]   pool: Pointer to thread-safe memory pool structure
 * \param[in]       ptr: Pointer to block to free
 */
void xgl_mempool_ts_free(xgl_mempool_ts_t* pool, void* ptr);

/**
 * \brief           Get number of free blocks in thread-safe pool
 * \param[in]       pool: Pointer to thread-safe memory pool structure
 * \return          Number of free blocks
 */
size_t xgl_mempool_ts_get_free_count(xgl_mempool_ts_t* pool);

/**
 * \brief           Get number of used blocks in thread-safe pool
 * \param[in]       pool: Pointer to thread-safe memory pool structure
 * \return          Number of used blocks
 */
size_t xgl_mempool_ts_get_used_count(xgl_mempool_ts_t* pool);

/**
 * \brief           Get peak number of blocks used in thread-safe pool
 * \param[in]       pool: Pointer to thread-safe memory pool structure
 * \return          Peak number of blocks used
 */
size_t xgl_mempool_ts_get_peak_used(xgl_mempool_ts_t* pool);

/**
 * \brief           Check if thread-safe memory pool is empty
 * \param[in]       pool: Pointer to thread-safe memory pool structure
 * \return          true if all blocks are free, false otherwise
 */
bool xgl_mempool_ts_is_empty(xgl_mempool_ts_t* pool);

/**
 * \brief           Check if thread-safe memory pool is full
 * \param[in]       pool: Pointer to thread-safe memory pool structure
 * \return          true if no free blocks, false otherwise
 */
bool xgl_mempool_ts_is_full(xgl_mempool_ts_t* pool);

/**
 * \brief           Reset thread-safe pool statistics
 * \param[in,out]   pool: Pointer to thread-safe memory pool structure
 */
void xgl_mempool_ts_reset_stats(xgl_mempool_ts_t* pool);

#endif /* XGL_THREAD_SAFE */

#ifdef __cplusplus
}
#endif

#endif /* XGL_MEMPOOL_H */
