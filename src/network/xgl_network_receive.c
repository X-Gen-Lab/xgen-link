/**
 * \file            xgl_network_receive.c
 * \brief           Network receive and forwarding path implementation
 */

#include "xgl/internal/xgl_route.h"
#include "xgl/internal/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl_network_internal.h"

static void network_count_rx_drop(xgl_network_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->rx_dropped++;
    }
}

/**
 * \brief           Deliver connection identity and borrowed payload upstream
 * \param[in,out]   ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       metadata: Validated frame view
 * \return          Upper layer acceptance result, or XGL_OK without a receiver
 */
static xgl_error_t
network_deliver_local(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                      const xgl_wire_frame_view_t* metadata) {
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += metadata->payload_len;
    }

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
                           .fragment = metadata->fragment,
                           .priority = metadata->priority,
                           .ttl = metadata->header.ttl,
                           .traffic_class = metadata->header.traffic_class,
                           .data = &packet_data,
                           .extensions = metadata->extensions,
                           .extensions_len = metadata->extensions_len,
                           .phy = NULL};

    return ctx->upper_layer->receive(ctx->upper_layer->ctx, handle, &packet);
}

/**
 * \brief           Decode a complete frame before local delivery or forwarding
 * \param[in,out]   ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       frame_buf: Complete frame bytes
 * \param[in]       frame_len: Available bytes including the final CRC
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_network_receive(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const uint8_t* frame_buf, size_t frame_len) {
    if (ctx == NULL || frame_buf == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_wire_frame_view_t metadata;
    xgl_error_t err =
        xgl_wire_decode_frame(&metadata, frame_buf, frame_len, NULL);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        return err;
    }

    return xgl_network_receive_view(ctx, handle, &metadata);
}

/**
 * \brief           Route a previously validated frame view without decoding
 *                  again
 * \param[in,out]   ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       metadata: Borrowed view from the trusted internal RX path
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_network_receive_view(xgl_network_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_wire_frame_view_t* metadata) {
    if (ctx == NULL || metadata == NULL || metadata->frame_buf == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!xgl_network_validate_address(ctx, metadata->header.target_id,
                                      metadata->header.source_id)) {
        network_count_rx_drop(ctx);
        return XGL_ERR_INVALID_PARAM;
    }

    if (xgl_network_is_local(ctx, metadata->header.target_id)) {
#if XGL_FEATURE_AUTH
        if (ctx->security != NULL) {
            xgl_error_t err =
                xgl_security_verify_frame(ctx->security, metadata);
            if (err != XGL_OK) {
                network_count_rx_drop(ctx);
                return err;
            }
        } else if (ctx->auth_required || metadata->authenticated) {
            network_count_rx_drop(ctx);
            return XGL_ERR_INVALID_FRAME;
        }
#else
        if (metadata->authenticated) {
            network_count_rx_drop(ctx);
            return XGL_ERR_UNSUPPORTED;
        }
#endif
        return network_deliver_local(ctx, handle, metadata);
    }

#if XGL_FEATURE_FORWARDING
    return xgl_network_forward(ctx, handle, metadata->frame_buf,
                               metadata->frame_len, metadata);
#else
    network_count_rx_drop(ctx);
    return XGL_ERR_ROUTE_NOT_FOUND;
#endif
}
