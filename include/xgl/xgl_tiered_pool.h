/**
 * \file            xgl_tiered_pool.h
 * \brief           Tiered memory pool for efficient allocation
 * \author          Nexus Team
 */

#ifndef XGL_TIERED_POOL_H
#define XGL_TIERED_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include "xgl_mempool.h"

/*---------------------------------------------------------------------------*/
/* Tiered Pool Configuration                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Small pool block size (<=64 bytes)
 */
#define XGL_TIERED_POOL_SMALL_SIZE 64

/**
 * \brief           Medium pool block size (<=256 bytes)
 */
#define XGL_TIERED_POOL_MEDIUM_SIZE 256

/**
 * \brief           Large pool block size (<=1024 bytes)
 */
#define XGL_TIERED_POOL_LARGE_SIZE 1024

/*---------------------------------------------------------------------------*/
/* Tiered Pool Structure                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Tiered memory pool structure
 */
typedef struct {
    xgl_mempool_t small_pool;   /**< Small pool (<=64 bytes) */
    xgl_mempool_t medium_pool;  /**< Medium pool (<=256 bytes) */
    xgl_mempool_t large_pool;   /**< Large pool (<=1024 bytes) */
    
    uint8_t* small_buffer;      /**< Buffer for small pool */
    uint8_t* medium_buffer;     /**< Buffer for medium pool */
    uint8_t* large_buffer;      /**< Buffer for large pool */
    
    size_t small_count;         /**< Number of small blocks */
    size_t medium_count;        /**< Number of medium blocks */
    size_t large_count;         /**< Number of large blocks */
    bool owns_buffers;          /**< True when buffers were allocated internally */
} xgl_tiered_pool_t;

/*---------------------------------------------------------------------------*/
/* Tiered Pool Initialization                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize tiered memory pool
 * \param[in,out]   pool: Pointer to tiered pool structure
 * \param[in]       small_count: Number of small blocks
 * \param[in]       medium_count: Number of medium blocks
 * \param[in]       large_count: Number of large blocks
 * \return          0 on success, -1 on error
 */
int xgl_tiered_pool_init(xgl_tiered_pool_t* pool, size_t small_count,
                         size_t medium_count, size_t large_count);

/**
 * \brief           Initialize tiered memory pool from application buffers
 * \details         This no-heap path is intended for production MCU profiles.
 *                  Non-zero block counts require a non-NULL matching buffer
 *                  sized count * XGL_TIERED_POOL_*_SIZE bytes.
 * \param[in,out]   pool: Pointer to tiered pool structure
 * \param[in]       small_buffer: Buffer for small blocks, or NULL if count is 0
 * \param[in]       small_count: Number of small blocks
 * \param[in]       medium_buffer: Buffer for medium blocks, or NULL if count is 0
 * \param[in]       medium_count: Number of medium blocks
 * \param[in]       large_buffer: Buffer for large blocks, or NULL if count is 0
 * \param[in]       large_count: Number of large blocks
 * \return          0 on success, -1 on error
 */
int xgl_tiered_pool_init_static(xgl_tiered_pool_t* pool,
                                uint8_t* small_buffer,
                                size_t small_count,
                                uint8_t* medium_buffer,
                                size_t medium_count,
                                uint8_t* large_buffer,
                                size_t large_count);

/**
 * \brief           Destroy tiered memory pool
 * \param[in,out]   pool: Pointer to tiered pool structure
 */
void xgl_tiered_pool_destroy(xgl_tiered_pool_t* pool);

/*---------------------------------------------------------------------------*/
/* Tiered Pool Operations                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Smart allocation (selects appropriate pool)
 * \param[in,out]   pool: Pointer to tiered pool structure
 * \param[in]       size: Size of memory to allocate
 * \return          Pointer to allocated memory, NULL if all pools exhausted
 */
void* xgl_tiered_pool_alloc(xgl_tiered_pool_t* pool, size_t size);

/**
 * \brief           Free memory back to tiered pool
 * \param[in,out]   pool: Pointer to tiered pool structure
 * \param[in]       ptr: Pointer to memory to free
 * \param[in]       size: Size of memory being freed
 */
void xgl_tiered_pool_free(xgl_tiered_pool_t* pool, void* ptr, size_t size);

/*---------------------------------------------------------------------------*/
/* Tiered Pool Query Operations                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get total free memory across all pools
 * \param[in]       pool: Pointer to tiered pool structure
 * \return          Total free memory in bytes
 */
size_t xgl_tiered_pool_get_free_memory(const xgl_tiered_pool_t* pool);

/**
 * \brief           Get total used memory across all pools
 * \param[in]       pool: Pointer to tiered pool structure
 * \return          Total used memory in bytes
 */
size_t xgl_tiered_pool_get_used_memory(const xgl_tiered_pool_t* pool);

/**
 * \brief           Get peak memory usage across all pools
 * \param[in]       pool: Pointer to tiered pool structure
 * \return          Peak memory usage in bytes
 */
size_t xgl_tiered_pool_get_peak_memory(const xgl_tiered_pool_t* pool);

/**
 * \brief           Reset statistics for all pools
 * \param[in,out]   pool: Pointer to tiered pool structure
 */
void xgl_tiered_pool_reset_stats(xgl_tiered_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* XGL_TIERED_POOL_H */
