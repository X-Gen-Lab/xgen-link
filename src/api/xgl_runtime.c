/**
 * \file            xgl_runtime.c
 * \brief           Instance scheduling with module-owned deadlines and state
 * \author          X-Gen Lab
 */
#include <xgl/xgl.h>
#include <xgl/xgl_config.h>

#include "xgl_instance_internal.h"

/**
 * \brief           Query the next relative link or transport deadline
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
        const uint32_t elapsed = now_ms - link->last_poll_ms;
        uint32_t remaining =
            (!link->polled || elapsed >= link->poll_interval_ms)
                ? 0U
                : link->poll_interval_ms - elapsed;
        if (!found || remaining < next) {
            next = remaining;
            found = true;
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
    xgl_error_t first_error = XGL_OK;
    for (size_t i = 0; i < handle->link_count && budget->rx_bytes != 0U; ++i) {
        xgl_instance_link_t* link = &handle->links[i];
        if (link->polled &&
            now_ms - link->last_poll_ms < link->poll_interval_ms) {
            continue;
        }
        link->polled = true;
        link->last_poll_ms = now_ms;
        error = xgl_datalink_poll_parser(
            &handle->layers.datalink_ctx, &link->parser, link->phy, now_ms,
            budget->receive_timeout_ms, budget->rx_bytes);
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
