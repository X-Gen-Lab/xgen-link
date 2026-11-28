/**
 * \file            xgl_network_receive.c
 * \brief           Network receive and forwarding path implementation
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-16
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "network/xgl_route.h"
#include "wire/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl_network_internal.h"

/**
 * \brief           Deliver connection identity and borrowed payload upstream
 * \details         Pass the upper layer acceptance result back to the caller.
 *                  With no receiver, this endpoint succeeds.
 */
static xgl_error_t
network_deliver_local(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                      const xgl_wire_frame_view_t* metadata) {
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += metadata->payload_len;
    }
#endif

    if (ctx->upper_layer == NULL || ctx->upper_layer->receive == NULL) {
        return XGL_OK;
    }

    xgl_packet_data_t packet_data = {.data_len = metadata->payload_len,
                                     .data = metadata->payload};

    xgl_packet_t packet = {.source_id = metadata->header.source_id,
                           .target_id = metadata->header.target_id,
                           .connection_id = metadata->header.connection_id,
                           .packet_number = metadata->header.packet_number,
                           .session_epoch = metadata->session_epoch,
                           .packet_type = metadata->header.packet_type,
                           .flags = metadata->header.flags,
                           .data_type = metadata->data_type,
                           .reliable = metadata->reliable,
                           .priority = metadata->priority,
                           .data = &packet_data,
                           .extensions = metadata->extensions,
                           .extensions_len = metadata->extensions_len};

    return ctx->upper_layer->receive(ctx->upper_layer->ctx, handle, &packet);
}

/**
 * \brief           Decode a complete frame before local delivery or forwarding
 * \details         The input covers the complete frame through its CRC.
 */
xgl_error_t xgl_network_receive(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const uint8_t* frame_buf, size_t frame_len) {
    if (ctx == NULL || frame_buf == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_wire_frame_view_t metadata;
    xgl_error_t err = XGL_ERR_INVALID_FRAME;
    if (ctx->max_frame_size == 0U || frame_len <= ctx->max_frame_size) {
        err = xgl_wire_decode_frame(&metadata, frame_buf, frame_len, NULL);
    }
    if (err != XGL_OK) {
#if XGL_FEATURE_STATISTICS
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
#endif
        return err;
    }

    return xgl_network_receive_view(ctx, handle, &metadata);
}

/**
 * \brief           Route a previously validated frame view without decoding
 *                  again
 * \details         The decoder has already validated this borrowed view.
 */
xgl_error_t
xgl_network_receive_mutable_view(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                 const xgl_wire_frame_view_t* metadata,
                                 uint8_t* mutable_frame) {
    if (ctx == NULL || metadata == NULL || metadata->frame_buf == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!xgl_network_validate_address(ctx, metadata->header.target_id,
                                      metadata->header.source_id)) {
        xgl_network_count_rx_drop(ctx);
        return XGL_ERR_INVALID_PARAM;
    }

    if (xgl_network_is_local(ctx, metadata->header.target_id)) {
#if XGL_FEATURE_AUTH
        if (ctx->security != NULL) {
            xgl_error_t err =
                xgl_security_verify_frame(ctx->security, metadata);
            if (err != XGL_OK) {
                xgl_network_count_rx_drop(ctx);
                return err;
            }
        } else if (metadata->authenticated) {
            xgl_network_count_rx_drop(ctx);
            return XGL_ERR_INVALID_FRAME;
        }
#else
        if (metadata->authenticated) {
            xgl_network_count_rx_drop(ctx);
            return XGL_ERR_UNSUPPORTED;
        }
#endif
        return network_deliver_local(ctx, handle, metadata);
    }

#if XGL_FEATURE_FORWARDING
    return xgl_network_forward_mutable(ctx, handle, metadata, mutable_frame);
#else
    (void)mutable_frame;
    xgl_network_count_rx_drop(ctx);
    return XGL_ERR_ROUTE_NOT_FOUND;
#endif
}

xgl_error_t xgl_network_receive_view(xgl_network_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_wire_frame_view_t* metadata) {
    return xgl_network_receive_mutable_view(ctx, handle, metadata, NULL);
}
