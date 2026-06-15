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
#include "xgl/xgl_types.h"

#ifndef XGL_ALLOW_FALLBACK_MALLOC
#define XGL_ALLOW_FALLBACK_MALLOC 1
#endif

/*---------------------------------------------------------------------------*/
/* Default Allocator                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get default allocator (malloc/free wrapper)
 * \return          Pointer to default allocator structure
 * \note            The default allocator uses standard malloc/free when
 *                  XGL_ALLOW_FALLBACK_MALLOC is 1. Strict no-heap builds
 *                  return NULL.
 */
xgl_allocator_t* xgl_allocator_get_default(void);

/*---------------------------------------------------------------------------*/
/* Allocator Wrapper Functions                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using provided allocator
 * \param[in]       allocator: Pointer to allocator structure
 *                  (NULL = default when XGL_ALLOW_FALLBACK_MALLOC is 1)
 * \param[in]       size: Size in bytes to allocate
 * \return          Pointer to allocated memory, NULL on failure
 */
void* xgl_alloc(xgl_allocator_t* allocator, size_t size);

/**
 * \brief           Free memory using provided allocator
 * \param[in]       allocator: Pointer to allocator structure
 *                  (NULL = default when XGL_ALLOW_FALLBACK_MALLOC is 1)
 * \param[in]       ptr: Pointer to memory to free
 */
void xgl_free(xgl_allocator_t* allocator, void* ptr);

/*---------------------------------------------------------------------------*/
/* Memory Tracking Allocator                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocation accounting phase
 */
typedef enum {
    XGL_ALLOCATOR_PHASE_INIT = 0,       /**< Instance creation and initialization */
    XGL_ALLOCATOR_PHASE_RUNTIME_TX,     /**< Steady-state transmit path */
    XGL_ALLOCATOR_PHASE_RUNTIME_RX,     /**< Steady-state receive path */
    XGL_ALLOCATOR_PHASE_RELIABLE,       /**< Reliable retransmission storage */
    XGL_ALLOCATOR_PHASE_FRAGMENT,       /**< Fragmentation and reassembly */
    XGL_ALLOCATOR_PHASE_COUNT
} xgl_allocator_phase_t;

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
 * \brief           Per-phase allocation statistics
 */
typedef struct {
    xgl_allocator_stats_t phase[XGL_ALLOCATOR_PHASE_COUNT];
} xgl_allocator_phase_stats_t;

/**
 * \brief           Tracking allocator structure
 */
typedef struct {
    xgl_allocator_t base;           /**< Base allocator interface */
    xgl_allocator_t* underlying;    /**< Underlying allocator */
    xgl_allocator_stats_t stats;    /**< Allocation statistics */
    xgl_allocator_phase_stats_t phase_stats; /**< Per-phase statistics */
    xgl_allocator_phase_t current_phase; /**< Current accounting phase */
} xgl_tracking_allocator_t;

/**
 * \brief           Initialize tracking allocator
 * \param[in,out]   tracker: Pointer to tracking allocator structure
 * \param[in]       underlying: Underlying allocator
 *                  (NULL = default when XGL_ALLOW_FALLBACK_MALLOC is 1)
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
 * \brief           Set current tracking phase
 * \param[in,out]   tracker: Pointer to tracking allocator structure
 * \param[in]       phase: Allocation phase
 */
void xgl_tracking_allocator_set_phase(xgl_tracking_allocator_t* tracker,
                                      xgl_allocator_phase_t phase);

/**
 * \brief           Get per-phase tracking statistics
 * \param[in]       tracker: Pointer to tracking allocator structure
 * \param[out]      stats: Pointer to per-phase statistics structure to fill
 */
void xgl_tracking_allocator_get_phase_stats(
    const xgl_tracking_allocator_t* tracker,
    xgl_allocator_phase_stats_t* stats);

/**
 * \brief           Get base allocator interface from tracking allocator
 * \param[in]       tracker: Pointer to tracking allocator structure
 * \return          Pointer to base allocator interface
 * \note            Use with xgl_alloc()/xgl_free(); direct callback calls do
 *                  not carry tracker context and fail closed.
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
