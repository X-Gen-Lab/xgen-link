/**
 * \file            xgl_stats.c
 * \brief           Statistics collection implementation
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

#include <string.h>

#include "xgl_instance_internal.h"

/*---------------------------------------------------------------------------*/
/* Statistics API Implementation                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get protocol statistics
 * \details         The caller serializes access to the instance.
 */
xgl_error_t xgl_stats_get(xgl_handle_t handle, xgl_statistics_t* stats) {
    /* Validate parameters */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    const struct xgl_instance* inst = handle;

    /* Check if instance is initialized */
    if (!inst->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    /* Copy statistics structure */
    memcpy(stats, &inst->stats, sizeof(xgl_statistics_t));

    return XGL_OK;
}

/**
 * \brief           Reset protocol statistics
 * \details         Resets all statistics counters to zero.
 * \note            The caller serializes access to the instance.
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

    /* Reset all statistics to zero */
    memset(&inst->stats, 0, sizeof(xgl_statistics_t));

    return XGL_OK;
}
