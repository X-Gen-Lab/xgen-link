/**
 * \file            xgl_runtime.c
 * \brief           Instance scheduling with module-owned deadlines and state
 * \author          X-Gen Lab
 */
#include <xgl/xgl.h>
#include <xgl/xgl_config.h>

#include "xgl_instance_internal.h"

/**
 * \brief           Compute the next polling delay from existing link state
 */
static inline uint32_t poll_timeout(const xgl_instance_link_t* link,
                                    uint32_t now_ms) {
    const uint32_t elapsed = now_ms - link->last_poll_ms;
    return !link->polled || elapsed >= link->poll_interval_ms
               ? 0U
               : link->poll_interval_ms - elapsed;
}

/* The current instant and duration keep their explicit millisecond units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Measure an active parser deadline without changing policy
 */
static inline bool parser_timeout(const xgl_instance_link_t* link,
                                  uint32_t now_ms, uint32_t timeout_ms,
                                  uint32_t* remaining) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (xgl_parser_has_pending(&link->parser)) {
        *remaining = 0U;
        return true;
    }
#endif
    if (timeout_ms == 0U || link->parser.state == XGL_PARSE_MAGIC ||
        link->parser.state == XGL_PARSE_COMPLETE) {
        return false;
    }
    const uint32_t elapsed = now_ms - link->parser.timestamp;
    *remaining = elapsed >= timeout_ms ? 0U : timeout_ms - elapsed;
    return true;
}

/**
 * \brief           Include one deadline in a caller-local minimum
 */
static inline void include_timeout(uint32_t candidate, uint32_t* next,
                                   bool* found) {
    if (!*found || candidate < *next) {
        *next = candidate;
        *found = true;
    }
}

/** \brief           Collect transport and link deadlines for the chosen policy.
 */
static bool instance_next_timeout(const struct xgl_instance* instance,
                                  uint32_t now_ms, bool include_polling,
                                  uint32_t receive_timeout_ms,
                                  uint32_t* delay_ms) {
    uint32_t next = 0U;
    bool found = xgl_transport_next_timeout(&instance->layers.transport_ctx,
                                            now_ms, &next);
    for (size_t i = 0U; i < instance->link_count; ++i) {
        const xgl_instance_link_t* link = &instance->links[i];
        if (include_polling) {
            include_timeout(poll_timeout(link, now_ms), &next, &found);
        }
        uint32_t remaining;
        if (parser_timeout(link, now_ms, receive_timeout_ms, &remaining)) {
            include_timeout(remaining, &next, &found);
        }
    }
    if (found) {
        *delay_ms = next;
    }
    return found;
}

/**
 * \brief           Include every link using the most recent step policy
 */
bool xgl_next_timeout(xgl_handle_t handle, uint32_t now_ms,
                      uint32_t* delay_ms) {
    if (handle == NULL || !handle->initialized || delay_ms == NULL) {
        return false;
    }
    return instance_next_timeout(handle, now_ms, true,
                                 handle->parser_timeout_ms, delay_ms);
}

/**
 * \brief           Apply a proposed work policy only to the deadline query
 */
bool xgl_next_timeout_for_work(xgl_handle_t handle, uint32_t now_ms,
                               const xgl_work_budget_t* budget,
                               uint32_t* delay_ms) {
    if (handle == NULL || !handle->initialized || delay_ms == NULL ||
        budget == NULL || budget->receive_timeout_ms >= UINT32_C(0x80000000)) {
        return false;
    }
    return instance_next_timeout(handle, now_ms, budget->rx_bytes != 0U,
                                 budget->receive_timeout_ms, delay_ms);
}

/**
 * \brief           Poll each due PHY once, then advance transport maintenance
 */
xgl_error_t xgl_step(xgl_handle_t handle, uint32_t now_ms,
                     const xgl_work_budget_t* budget) {
#if XGL_FEATURE_ADVANCED_RUNTIME
    const xgl_execution_limits_t limits = {SIZE_MAX, SIZE_MAX};
    xgl_execution_result_t result;
    return xgl_step_bounded(handle, now_ms, budget, &limits, &result);
#else
    if (handle == NULL || budget == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    if (budget->receive_timeout_ms >= UINT32_C(0x80000000)) {
        return XGL_ERR_INVALID_PARAM;
    }
    xgl_error_t error;
    handle->layers.transport_ctx.current_time_ms = now_ms;
    handle->parser_timeout_ms = budget->receive_timeout_ms;
    xgl_error_t first_error = XGL_OK;
    for (size_t i = 0; i < handle->link_count; ++i) {
        xgl_instance_link_t* link = &handle->links[i];
        if (budget->rx_bytes == 0U ||
            (link->polled &&
             now_ms - link->last_poll_ms < link->poll_interval_ms)) {
            error = xgl_datalink_expire_parser(&handle->layers.datalink_ctx,
                                               &link->parser, now_ms,
                                               budget->receive_timeout_ms);
        } else {
            link->polled = true;
            link->last_poll_ms = now_ms;
            error = xgl_datalink_poll_parser(
                &handle->layers.datalink_ctx, &link->parser, link->phy, now_ms,
                budget->receive_timeout_ms, budget->rx_bytes);
        }
        if (first_error == XGL_OK && error != XGL_OK) {
            first_error = error;
        }
    }
    error = xgl_transport_run(&handle->layers.transport_ctx, handle, now_ms);
    if (first_error == XGL_OK) {
        first_error = error;
    }
    return first_error;
#endif
}

xgl_error_t xgl_step_bounded(xgl_handle_t handle, uint32_t now_ms,
                             const xgl_work_budget_t* budget,
                             const xgl_execution_limits_t* limits,
                             xgl_execution_result_t* result) {
    if (result != NULL) {
        result->pending = false;
    }
    if (handle == NULL || budget == NULL || limits == NULL || result == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    if (budget->receive_timeout_ms >= UINT32_C(0x80000000)) {
        return XGL_ERR_INVALID_PARAM;
    }
#if XGL_FEATURE_ADVANCED_RUNTIME
    handle->layers.transport_ctx.current_time_ms = now_ms;
    handle->parser_timeout_ms = budget->receive_timeout_ms;
    xgl_error_t first_error = XGL_OK;
    for (size_t i = 0U; i < handle->link_count; ++i) {
        xgl_instance_link_t* link = &handle->links[i];
        bool due = budget->rx_bytes != 0U &&
                   (!link->polled ||
                    now_ms - link->last_poll_ms >= link->poll_interval_ms);
        size_t remaining = limits->parser_candidates;
        bool polled = false;
        bool pending = false;
        xgl_error_t error = xgl_datalink_poll_parser_bounded(
            &handle->layers.datalink_ctx, &link->parser, link->phy, now_ms,
            budget->receive_timeout_ms, due ? budget->rx_bytes : 0U, &remaining,
            &polled, &pending);
        if (polled) {
            link->polled = true;
            link->last_poll_ms = now_ms;
        }
        result->pending = result->pending || pending;
        if (first_error == XGL_OK && error != XGL_OK) {
            first_error = error;
        }
    }
    bool pending = false;
    xgl_error_t error =
        xgl_transport_run_bounded(&handle->layers.transport_ctx, handle, now_ms,
                                  limits->tx_packets, &pending);
    result->pending = result->pending || pending;
    return first_error != XGL_OK ? first_error : error;
#else
    (void)now_ms;
    return XGL_ERR_UNSUPPORTED;
#endif
}
