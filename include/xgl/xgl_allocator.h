/**
 * \file            xgl_allocator.h
 * \brief           Custom allocator support and memory tracking
 * \author          Nexus Team
 */

#ifndef XGL_ALLOCATOR_H
#define XGL_ALLOCATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "xgl_types.h"

/*---------------------------------------------------------------------------*/
/* Default Allocator                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get default allocator (malloc/free wrapper)
 * \return          Pointer to default allocator structure
 * \note            The default allocator uses standard malloc/free
 */
xgl_allocator_t* xgl_allocator_get_default(void);

/*---------------------------------------------------------------------------*/
/* Allocator Wrapper Functions                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using provided allocator
 * \param[in]       allocator: Pointer to allocator structure (NULL = default)
 * \param[in]       size: Size in bytes to allocate
 * \return          Pointer to allocated memory, NULL on failure
 */
void* xgl_alloc(xgl_allocator_t* allocator, size_t size);

/**
 * \brief           Free memory using provided allocator
 * \param[in]       allocator: Pointer to allocator structure (NULL = default)
 * \param[in]       ptr: Pointer to memory to free
 */
void xgl_free(xgl_allocator_t* allocator, void* ptr);

/*---------------------------------------------------------------------------*/
/* Memory Tracking Allocator                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Tracking allocator statistics
 */
typedef struct {
    size_t total_allocated;         /**< Total bytes allocated */
    size_t total_freed;             /**< Total bytes freed */
    size_t current_allocated;       /**< Current bytes allocated */
    size_t peak_allocated;          /**< Peak bytes allocated */
    size_t alloc_count;             /**< Number of allocations */
    size_t free_count;              /**< Number of frees */
} xgl_allocator_stats_t;

/**
 * \brief           Tracking allocator structure
 */
typedef struct {
    xgl_allocator_t base;           /**< Base allocator interface */
    xgl_allocator_t* underlying;    /**< Underlying allocator */
    xgl_allocator_stats_t stats;    /**< Allocation statistics */
} xgl_tracking_allocator_t;

/**
 * \brief           Initialize tracking allocator
 * \param[in,out]   tracker: Pointer to tracking allocator structure
 * \param[in]       underlying: Underlying allocator (NULL = default)
 * \return          0 on success, -1 on error
 */
int xgl_tracking_allocator_init(xgl_tracking_allocator_t* tracker,
                                xgl_allocator_t* underlying);

/**
 * \brief           Get tracking allocator statistics
 * \param[in]       tracker: Pointer to tracking allocator structure
 * \param[out]      stats: Pointer to statistics structure to fill
 */
void xgl_tracking_allocator_get_stats(const xgl_tracking_allocator_t* tracker,
                                     xgl_allocator_stats_t* stats);

/**
 * \brief           Reset tracking allocator statistics
 * \param[in,out]   tracker: Pointer to tracking allocator structure
 */
void xgl_tracking_allocator_reset_stats(xgl_tracking_allocator_t* tracker);

/**
 * \brief           Get base allocator interface from tracking allocator
 * \param[in]       tracker: Pointer to tracking allocator structure
 * \return          Pointer to base allocator interface
 */
xgl_allocator_t* xgl_tracking_allocator_get_interface(
    xgl_tracking_allocator_t* tracker);

/**
 * \brief           Allocate memory using tracking allocator
 * \param[in,out]   tracker: Pointer to tracking allocator structure
 * \param[in]       size: Size in bytes to allocate
 * \return          Pointer to allocated memory, NULL on failure
 * \note            This function updates allocation statistics
 */
void* xgl_tracking_alloc(xgl_tracking_allocator_t* tracker, size_t size);

/**
 * \brief           Free memory using tracking allocator
 * \param[in,out]   tracker: Pointer to tracking allocator structure
 * \param[in]       ptr: Pointer to memory to free
 * \note            This function updates allocation statistics
 */
void xgl_tracking_free(xgl_tracking_allocator_t* tracker, void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* XGL_ALLOCATOR_H */
