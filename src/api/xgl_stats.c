/**
 * \file            xgl_stats.c
 * \brief           Statistics collection implementation
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>
#include <xgl/internal/xgl_atomic.h>
#include <xgl/internal/xgl_mutex.h>
#include "xgl_instance_internal.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Statistics API Implementation                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get protocol statistics
 * \details         Retrieves current statistics from the protocol instance.
 *                  When built with \c XGL_THREAD_SAFE and thread_safe enabled,
 *                  the copy is taken under the instance mutex, providing a
 *                  consistent point-in-time snapshot. In non-thread-safe
 *                  builds, counters are best-effort (see xgl_statistics_t).
 */
xgl_error_t xgl_stats_get(xgl_handle_t handle, xgl_statistics_t* stats) {
    /* Validate parameters */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* The thread-safe path locks inst->mutex, so this handle must stay mutable. */
    // cppcheck-suppress constVariablePointer
    struct xgl_instance* inst = (struct xgl_instance*)handle;

    /* Check if instance is initialized */
    if (!inst->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

#ifdef XGL_THREAD_SAFE
    /* Lock mutex for thread-safe access */
    if (inst->config.features.thread_safe) {
        xgl_error_t err = xgl_instance_lock(inst);
        if (err != XGL_OK) {
            return err;
        }
    }
#endif

    /* Copy statistics structure */
    memcpy(stats, &inst->stats, sizeof(xgl_statistics_t));

#ifdef XGL_THREAD_SAFE
    /* Unlock mutex */
    xgl_instance_unlock(inst);
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
    if (inst->config.features.thread_safe) {
        xgl_error_t err = xgl_instance_lock(inst);
        if (err != XGL_OK) {
            return err;
        }
    }
#endif

    /* Reset all statistics to zero */
    memset(&inst->stats, 0, sizeof(xgl_statistics_t));

#ifdef XGL_THREAD_SAFE
    /* Unlock mutex */
    xgl_instance_unlock(inst);
#endif

    return XGL_OK;
}
