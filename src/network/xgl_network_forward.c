/**
 * \file            xgl_network_forward.c
 * \brief           Routed frame forwarding through the datalink submission
 * \author          X-Gen Lab
 */

#include "internal/xgl_allocator.h"
#include <internal/xgl_diagnostics.h>
#include <string.h>
#include <wire/xgl_wire_bytes.h>
#include <wire/xgl_wire_rx_internal.h>

#include "xgl_network_internal.h"

#if XGL_FEATURE_FORWARDING
static xgl_error_t network_lookup_forward_route(xgl_network_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                uint16_t target_id,
                                                xgl_route_item_t** route) {
    *route = xgl_route_table_lookup(ctx->route_table, target_id);
    if (*route != NULL) {
        return XGL_OK;
    }

    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle, XGL_ERR_ROUTE_NOT_FOUND,
                            XGL_ERROR_MESSAGE("No route for forwarding"),
                            ctx->callback_user_data);
    }

    xgl_network_count_rx_drop(ctx);
    return XGL_ERR_ROUTE_NOT_FOUND;
}

static xgl_error_t network_validate_forward_ttl(xgl_network_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                uint8_t ttl) {
    if (ttl > 1U) {
        return XGL_OK;
    }

    xgl_network_count_rx_drop(ctx);
    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle, XGL_ERR_TTL_EXPIRED,
                            XGL_ERROR_MESSAGE("Packet TTL expired"),
                            ctx->callback_user_data);
    }
    return XGL_ERR_TTL_EXPIRED;
}

static xgl_error_t network_validate_forward_size(xgl_network_ctx_t* ctx,
                                                 const xgl_route_item_t* route,
                                                 size_t frame_len) {
    if (frame_len > xgl_route_frame_limit(route, XGL_DATALINK_MAX_FRAME_SIZE)) {
        xgl_network_count_rx_drop(ctx);
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    return XGL_OK;
}

/**
 * \brief           Forward a frame using parser storage or bounded scratch
 * \param[in,out]   ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       metadata: Validated incoming frame view
 * \param[in,out]   mutable_frame: Parser-owned bytes, restored before return
 * \return          XGL_OK on success, error code otherwise
 * \note            The selected PHY must finish reading before tx returns.
 */
xgl_error_t xgl_network_forward_mutable(xgl_network_ctx_t* ctx,
                                        xgl_handle_t handle,
                                        const xgl_wire_frame_view_t* metadata,
                                        uint8_t* mutable_frame) {
    const uint8_t* frame_buf = metadata->frame_buf;
    const size_t frame_len = metadata->frame_len;
    xgl_route_item_t* route = NULL;
    xgl_error_t err = network_lookup_forward_route(
        ctx, handle, metadata->header.target_id, &route);
    if (err != XGL_OK) {
        return err;
    }

    err = network_validate_forward_ttl(ctx, handle, metadata->header.ttl);
    if (err != XGL_OK) {
        return err;
    }

    err = network_validate_forward_size(ctx, route, frame_len);
    if (err != XGL_OK) {
        return err;
    }

    const bool owns_scratch = mutable_frame != frame_buf;
    uint8_t* forward_buf = owns_scratch
                               ? (uint8_t*)xgl_alloc(ctx->allocator, frame_len)
                               : mutable_frame;
    if (forward_buf == NULL) {
        xgl_network_count_rx_drop(ctx);
#if XGL_FEATURE_STATISTICS
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
#endif
        return XGL_ERR_NO_MEMORY;
    }

    uint8_t* frame_crc = forward_buf + frame_len - XGL_CRC16_SIZE;
    uint16_t original_frame_crc = 0U;
    if (owns_scratch) {
        memcpy(forward_buf, frame_buf, frame_len);
    } else {
        original_frame_crc = xgl_deserialize_u16_le(frame_crc);
    }
    xgl_wire_decrement_frame_ttl(forward_buf, frame_len);

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        err = XGL_ERR_INVALID_PARAM;
    } else {
        xgl_frame_tx_message_t message = {
            .phy = route->phy,
            .serialized = forward_buf,
            .serialized_len = frame_len,
        };
        err = ctx->lower_layer->send(ctx->lower_layer->ctx, handle, &message);
    }
    if (owns_scratch) {
        xgl_free(ctx->allocator, forward_buf);
    } else {
        forward_buf[XGL_WIRE_TTL_OFFSET] = metadata->header.ttl;
        xgl_serialize_u16_le(forward_buf + XGL_WIRE_BASE_HEADER_SIZE -
                                 XGL_CRC16_SIZE,
                             metadata->header.header_crc16);
        xgl_serialize_u16_le(frame_crc, original_frame_crc);
    }
    if (err != XGL_OK) {
#if XGL_FEATURE_STATISTICS
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
#endif
        return err;
    }
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += metadata->payload_len;
    }
#endif
    return XGL_OK;
}

#endif
