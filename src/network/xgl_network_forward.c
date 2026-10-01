/**
 * \file            xgl_network_forward.c
 * \brief           Routed frame forwarding through the datalink submission
 * \author          X-Gen Lab
 */

#include <string.h>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>
#include <xgen/memory/allocator.h>

#include "xgl_network_internal.h"

static void network_count_rx_drop(xgl_network_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->rx_dropped++;
    }
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
xgl_error_t xgl_network_forward(xgl_network_ctx_t* ctx, xgl_handle_t handle,
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
    xgm_free(ctx->allocator, forward_buf);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return XGL_ERR_TX_FAILED;
    }
    return XGL_OK;
}

#endif
