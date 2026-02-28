/**
 * \file            xgl_stats.c
 * \brief           Statistics collection implementation
 * \author          Nexus Team
 */

#include <xgl/xgl.h>
#include <xgl/xgl_atomic.h>
#include <xgl/xgl_mutex.h>
#include "xgl_instance_internal.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Statistics API Implementation                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get protocol statistics
 * \details         Retrieves current statistics from the protocol instance.
 *                  Uses atomic operations to ensure thread-safe reads.
 * \note            Statistics are copied atomically to prevent inconsistencies
 */
xgl_error_t xgl_stats_get(xgl_handle_t handle, xgl_statistics_t* stats) {
    /* Validate parameters */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    struct xgl_instance* inst = (struct xgl_instance*)handle;
    
    /* Check if instance is initialized */
    if (!inst->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
#ifdef XGL_THREAD_SAFE
    /* Lock mutex for thread-safe access */
    xgl_error_t err = xgl_mutex_lock(&inst->mutex);
    if (err != XGL_OK) {
        return err;
    }
#endif
    
    /* Copy statistics structure */
    memcpy(stats, &inst->stats, sizeof(xgl_statistics_t));
    
#ifdef XGL_THREAD_SAFE
    /* Unlock mutex */
    xgl_mutex_unlock(&inst->mutex);
#endif
    
    return XGL_OK;
}

/**
 * \brief           Reset protocol statistics
 * \details         Resets all statistics counters to zero.
 *                  Uses atomic operations to ensure thread-safe reset.
 * \note            This operation is atomic and thread-safe
 */
xgl_error_t xgl_stats_reset(xgl_handle_t handle) {
    /* Validate parameters */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    struct xgl_instance* inst = (struct xgl_instance*)handle;
    
    /* Check if instance is initialized */
    if (!inst->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
#ifdef XGL_THREAD_SAFE
    /* Lock mutex for thread-safe access */
    xgl_error_t err = xgl_mutex_lock(&inst->mutex);
    if (err != XGL_OK) {
        return err;
    }
#endif
    
    /* Reset all statistics to zero */
    memset(&inst->stats, 0, sizeof(xgl_statistics_t));
    
#ifdef XGL_THREAD_SAFE
    /* Unlock mutex */
    xgl_mutex_unlock(&inst->mutex);
#endif
    
    return XGL_OK;
}
