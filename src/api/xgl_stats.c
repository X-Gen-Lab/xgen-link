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
#if XGL_FEATURE_DIAGNOSTICS
    const xgl_transport_ctx_t* transport = &inst->layers.transport_ctx;
    stats->avg_rtt_ms = transport->rtt_sample_count == 0U
                            ? 0U
                            : (uint32_t)(transport->rtt_total_ms /
                                         transport->rtt_sample_count);
    stats->min_rtt_ms = transport->rtt_min_ms;
    stats->max_rtt_ms = transport->rtt_max_ms;
#endif

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

    /* Reset observations without changing reserved memory or protocol state. */
    const size_t reserved_bytes = inst->stats.memory_used;
    memset(&inst->stats, 0, sizeof(xgl_statistics_t));
    inst->stats.min_rtt_ms = UINT32_MAX;
    inst->stats.memory_used = reserved_bytes;
    inst->stats.memory_peak = reserved_bytes;
#if XGL_FEATURE_DIAGNOSTICS
    xgl_transport_ctx_t* transport = &inst->layers.transport_ctx;
    transport->rtt_total_ms = 0U;
    transport->rtt_sample_count = 0U;
    transport->rtt_min_ms = UINT32_MAX;
    transport->rtt_max_ms = 0U;
#endif

    return XGL_OK;
}
