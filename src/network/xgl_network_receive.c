/**
 * \file            xgl_network_receive.c
 * \brief           Network receive and forwarding path implementation
 */

#include <string.h>

#include "xgen/bytes/bytes.h"
#include "xgen/crc/crc.h"
#include "xgen/memory/allocator.h"
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
                            "No route for forwarding", ctx->callback_user_data);
    }

    network_count_rx_drop(ctx);
    return XGL_ERR_ROUTE_NOT_FOUND;
}

static xgl_error_t network_validate_forward_ttl(xgl_network_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                uint8_t ttl) {
    if (ttl > 1U) {
        return XGL_OK;
    }

    network_count_rx_drop(ctx);
    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle, XGL_ERR_TTL_EXPIRED, "Packet TTL expired",
                            ctx->callback_user_data);
    }
    return XGL_ERR_TTL_EXPIRED;
}

static xgl_error_t network_validate_forward_size(xgl_network_ctx_t* ctx,
                                                 const xgl_route_item_t* route,
                                                 size_t frame_len) {
    if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE ||
        frame_len > route->max_frame_size) {
        network_count_rx_drop(ctx);
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    return XGL_OK;
}

static xgl_error_t network_rewrite_forward_frame(
    xgl_network_ctx_t* ctx, const uint8_t* frame_buf, size_t frame_len,
    const xgl_wire_header_t* incoming_header, uint8_t* forward_buf) {
    memcpy(forward_buf, frame_buf, frame_len);

    xgl_wire_header_t wire_header = *incoming_header;
    wire_header.ttl = (uint8_t)(wire_header.ttl - 1U);
    if (xgl_wire_encode_header(forward_buf, frame_len, &wire_header) !=
        XGL_OK) {
        network_count_rx_drop(ctx);
        return XGL_ERR_INVALID_FRAME;
    }

    uint16_t forward_crc =
        xgcrc_crc16_modbus(forward_buf, frame_len - XGL_CRC16_SIZE);
    xgb_serialize_u16_le(&forward_buf[frame_len - XGL_CRC16_SIZE], forward_crc);
    return XGL_OK;
}

/**
 * \brief           Forward a frame using an allocator-owned temporary buffer
 * \param[in,out]   ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       frame_buf: Validated complete frame bytes
 * \param[in]       frame_len: Number of complete frame bytes
 * \param[in]       metadata: Validated incoming frame view
 * \return          XGL_OK on success, error code otherwise
 * \note            The selected PHY must finish reading before tx returns.
 */
static xgl_error_t
network_forward_remote(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                       const uint8_t* frame_buf, size_t frame_len,
                       const xgl_wire_frame_view_t* metadata) {
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

    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += metadata->payload_len;
    }

    err = network_validate_forward_size(ctx, route, frame_len);
    if (err != XGL_OK) {
        return err;
    }

    uint8_t* forward_buf = (uint8_t*)xgm_alloc(ctx->allocator, frame_len);
    if (forward_buf == NULL) {
        network_count_rx_drop(ctx);
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return XGL_ERR_NO_MEMORY;
    }

    err = network_rewrite_forward_frame(ctx, frame_buf, frame_len,
                                        &metadata->header, forward_buf);
    if (err != XGL_OK) {
        xgm_free(ctx->allocator, forward_buf);
        return err;
    }

    if (route->phy != NULL && route->phy->tx != NULL) {
        err = route->phy->tx(forward_buf, frame_len, route->phy->user_data);
        if (err != XGL_OK) {
            if (ctx->stats != NULL) {
                ctx->stats->tx_errors++;
            }
            xgm_free(ctx->allocator, forward_buf);
            return XGL_ERR_TX_FAILED;
        }
    }

    xgm_free(ctx->allocator, forward_buf);

    return XGL_OK;
}

#endif
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
    return network_forward_remote(ctx, handle, metadata->frame_buf,
                                  metadata->frame_len, metadata);
#else
    network_count_rx_drop(ctx);
    return XGL_ERR_ROUTE_NOT_FOUND;
#endif
}
