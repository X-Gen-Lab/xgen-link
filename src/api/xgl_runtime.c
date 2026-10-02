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

/**
 * \brief           Include every link using the most recent step policy
 */
bool xgl_next_timeout(xgl_handle_t handle, uint32_t now_ms,
                      uint32_t* delay_ms) {
    if (handle == NULL || !handle->initialized || delay_ms == NULL) {
        return false;
    }
    uint32_t next = 0U;
    bool found = xgl_transport_next_timeout(&handle->layers.transport_ctx,
                                            now_ms, &next);
    for (size_t i = 0; i < handle->link_count; ++i) {
        const xgl_instance_link_t* link = &handle->links[i];
        include_timeout(poll_timeout(link, now_ms), &next, &found);
        uint32_t remaining;
        if (parser_timeout(link, now_ms, handle->parser_timeout_ms,
                           &remaining)) {
            include_timeout(remaining, &next, &found);
        }
    }
    if (found) {
        *delay_ms = next;
    }
    return found;
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
    uint32_t next = 0U;
    bool found = xgl_transport_next_timeout(&handle->layers.transport_ctx,
                                            now_ms, &next);
    for (size_t i = 0; i < handle->link_count; ++i) {
        const xgl_instance_link_t* link = &handle->links[i];
        if (budget->rx_bytes != 0U) {
            include_timeout(poll_timeout(link, now_ms), &next, &found);
        }
        uint32_t remaining;
        if (parser_timeout(link, now_ms, budget->receive_timeout_ms,
                           &remaining)) {
            include_timeout(remaining, &next, &found);
        }
    }
    if (found) {
        *delay_ms = next;
    }
    return found;
}

/**
 * \brief           Poll each due PHY once, then advance transport maintenance
 */
xgl_error_t xgl_step(xgl_handle_t handle, uint32_t now_ms,
                     const xgl_work_budget_t* budget) {
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
}
