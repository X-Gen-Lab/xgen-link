/**
 * \file            xgl_network_receive.c
 * \brief           Network receive and forwarding path implementation
 */

#include "xgl_network_internal.h"

#include "xgl/internal/xgl_crc.h"
#include "xgl/internal/xgl_network_metadata.h"
#include "xgl/internal/xgl_route.h"
#include "xgl/internal/xgl_serialize.h"
#include "xgl/xgl_config.h"

#include <stdio.h>
#include <string.h>

static void network_count_rx_drop(xgl_network_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->rx_dropped++;
    }
}

static xgl_error_t network_deliver_local(xgl_network_ctx_t* ctx,
                                         xgl_handle_t handle,
                                         const xgl_network_frame_metadata_t* metadata) {
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += metadata->payload_len;
    }

    if (ctx->upper_layer == NULL || ctx->upper_layer->receive == NULL) {
        return XGL_OK;
    }

    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = metadata->payload_len,
        .data = metadata->payload,
        .owned_data = NULL
    };

    xgl_packet_t packet = {
        .source_id = metadata->header.source_id,
        .target_id = metadata->header.target_id,
        .session_id = (uint16_t)(metadata->header.connection_id & UINT16_MAX),
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
        .phy = NULL
    };

    return ctx->upper_layer->receive(ctx->upper_layer->ctx, handle, &packet);
}

static xgl_error_t network_lookup_forward_route(xgl_network_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                uint16_t target_id,
                                                xgl_route_item_t** route) {
    *route = xgl_route_table_lookup(ctx->route_table, target_id);
    if (*route != NULL) {
        return XGL_OK;
    }

    char error_msg[64];
    snprintf(error_msg,
             sizeof(error_msg),
             "No route for forwarding to target ID: %u",
             (unsigned int)target_id);

    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle,
                            XGL_ERR_ROUTE_NOT_FOUND,
                            error_msg,
                            ctx->callback_user_data);
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
        ctx->error_callback(handle,
                            XGL_ERR_TTL_EXPIRED,
                            "Packet TTL expired",
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

static xgl_error_t network_rewrite_forward_frame(xgl_network_ctx_t* ctx,
                                                 const uint8_t* frame_buf,
                                                 size_t frame_len,
                                                 const xgl_wire_header_t* incoming_header,
                                                 uint8_t* forward_buf) {
    memcpy(forward_buf, frame_buf, frame_len);

    xgl_wire_header_t wire_header = *incoming_header;
    wire_header.ttl = (uint8_t)(wire_header.ttl - 1U);
    if (xgl_wire_encode_header(forward_buf, frame_len, &wire_header) != XGL_OK) {
        network_count_rx_drop(ctx);
        return XGL_ERR_INVALID_FRAME;
    }

    if (network_resign_forwarded_frame(ctx,
                                       forward_buf,
                                       frame_len,
                                       &wire_header) != XGL_OK) {
        network_count_rx_drop(ctx);
        return XGL_ERR_INVALID_FRAME;
    }

    uint16_t forward_crc =
        xgl_crc16_modbus(forward_buf, frame_len - XGL_CRC16_SIZE);
    xgl_serialize_u16_le(&forward_buf[frame_len - XGL_CRC16_SIZE], forward_crc);
    return XGL_OK;
}

static xgl_error_t network_forward_remote(xgl_network_ctx_t* ctx,
                                          xgl_handle_t handle,
                                          const uint8_t* frame_buf,
                                          size_t frame_len,
                                          const xgl_network_frame_metadata_t* metadata) {
    xgl_route_item_t* route = NULL;
    xgl_error_t err = network_lookup_forward_route(ctx,
                                                   handle,
                                                   metadata->header.target_id,
                                                   &route);
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

    uint8_t forward_buf[XGL_DATALINK_MAX_FRAME_SIZE];
    err = network_rewrite_forward_frame(ctx,
                                        frame_buf,
                                        frame_len,
                                        &metadata->header,
                                        forward_buf);
    if (err != XGL_OK) {
        return err;
    }

    if (route->phy != NULL && route->phy->tx != NULL) {
        err = route->phy->tx(forward_buf, frame_len, route->phy->user_data);
        if (err != XGL_OK) {
            if (ctx->stats != NULL) {
                ctx->stats->tx_errors++;
            }
            return XGL_ERR_TX_FAILED;
        }
    }

    return XGL_OK;
}

xgl_error_t xgl_network_receive(xgl_network_ctx_t* ctx,
                                xgl_handle_t handle,
                                const uint8_t* frame_buf,
                                size_t frame_len) {
    if (ctx == NULL || frame_buf == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_network_frame_metadata_t metadata;
    xgl_error_t err = xgl_network_decode_frame_metadata(frame_buf,
                                                        frame_len,
                                                        &metadata);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        return err;
    }

    if (!xgl_network_validate_address(ctx,
                                      metadata.header.target_id,
                                      metadata.header.source_id)) {
        network_count_rx_drop(ctx);
        return XGL_ERR_INVALID_PARAM;
    }

    if (xgl_network_is_local(ctx, metadata.header.target_id)) {
        return network_deliver_local(ctx, handle, &metadata);
    }

    return network_forward_remote(ctx, handle, frame_buf, frame_len, &metadata);
}
