/**
 * \file            xgl_transport_send_plan.c
 * \brief           Transport send planning helpers
 */

#include "xgl/internal/xgl_transport_send.h"
#include "xgl/internal/xgl_frame.h"
#include "xgl/internal/xgl_route.h"

#include <string.h>

static uint16_t transport_effective_max_frame_size(const xgl_transport_ctx_t* ctx,
                                                   uint16_t target_id) {
    uint16_t max_frame_size = ctx->max_frame_size;

    if (ctx->route_table != NULL) {
        const xgl_route_item_t* route =
            xgl_route_table_lookup(ctx->route_table, target_id);
        if (route != NULL && route->max_frame_size < max_frame_size) {
            max_frame_size = route->max_frame_size;
        }
    }

    return max_frame_size;
}

xgl_error_t transport_build_send_plan(const xgl_transport_ctx_t* ctx,
                                      const xgl_tx_data_t* tx_data,
                                      xgl_transport_send_plan_t* plan) {
    if (ctx == NULL || tx_data == NULL || plan == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(plan, 0, sizeof(*plan));
    plan->max_frame_size =
        transport_effective_max_frame_size(ctx, tx_data->target_id);
    plan->app_extensions_len =
        (tx_data->data_type != 0U) ? XGL_DATA_TYPE_EXT_SIZE : 0U;

    size_t base_budget = 0U;
    if (!xgl_frame_payload_budget(plan->max_frame_size,
                                  0U,
                                  ctx->auth_tag_len,
                                  &base_budget)) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (!xgl_frame_payload_budget(plan->max_frame_size,
                                  plan->app_extensions_len,
                                  ctx->auth_tag_len,
                                  &plan->app_payload_budget)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (tx_data->data_len <= plan->app_payload_budget) {
        plan->fragment_extensions_len = plan->app_extensions_len;
        plan->fragment_payload_budget = plan->app_payload_budget;
        plan->fragment_count = 1U;
        return XGL_OK;
    }

    if (!ctx->enable_fragmentation) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    plan->needs_fragmentation = true;
    plan->fragment_extensions_len =
        plan->app_extensions_len + XGL_FRAGMENT_EXT_SIZE;
    if (plan->app_payload_budget <= XGL_FRAGMENT_EXT_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    plan->fragment_payload_budget =
        plan->app_payload_budget - XGL_FRAGMENT_EXT_SIZE;
    plan->fragment_count =
        (tx_data->data_len + plan->fragment_payload_budget - 1U) /
        plan->fragment_payload_budget;

    return XGL_OK;
}
