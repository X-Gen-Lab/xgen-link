/**
 * \file            xgl_network_send.c
 * \brief           Network send path implementation
 */

#include "xgl_network_internal.h"

#include "xgl/internal/xgl_frame.h"
#include "xgl/internal/xgl_network_metadata.h"
#include "xgl/internal/xgl_route.h"
#include "xgl/internal/xgl_wire.h"
#include "xgl/xgl_config.h"

#include <stdio.h>
#include <string.h>

static void network_count_tx_error(xgl_network_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

static xgl_error_t network_append_data_type_ext(uint8_t* extensions,
                                                size_t extensions_capacity,
                                                size_t* extensions_len,
                                                uint8_t data_type) {
    if (extensions == NULL || extensions_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (data_type == 0U) {
        return XGL_OK;
    }
    if (*extensions_len > extensions_capacity) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t bytes_written = 0U;
    xgl_error_t err = xgl_wire_encode_ext(&extensions[*extensions_len],
                                          extensions_capacity - *extensions_len,
                                          XGL_WIRE_EXT_DATA_TYPE,
                                          &data_type,
                                          1U,
                                          &bytes_written);
    if (err != XGL_OK) {
        return err;
    }
    *extensions_len += bytes_written;
    return XGL_OK;
}

static xgl_error_t network_copy_packet_extensions(uint8_t* extensions,
                                                  size_t extensions_capacity,
                                                  size_t* extensions_len,
                                                  const xgl_packet_t* packet) {
    if (extensions == NULL || extensions_len == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *extensions_len = 0U;
    if (packet->extensions != NULL && packet->extensions_len > 0U) {
        if (packet->extensions_len > extensions_capacity) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(extensions, packet->extensions, packet->extensions_len);
        *extensions_len = packet->extensions_len;
    }

    xgl_network_ext_metadata_t metadata;
    xgl_error_t err = xgl_network_decode_ext_metadata(extensions,
                                                      *extensions_len,
                                                      &metadata);
    if (err != XGL_OK) {
        return err;
    }
    if (metadata.data_type_found) {
        if (packet->data_type != 0U && packet->data_type != metadata.data_type) {
            return XGL_ERR_INVALID_PARAM;
        }
        return XGL_OK;
    }

    return network_append_data_type_ext(extensions,
                                        extensions_capacity,
                                        extensions_len,
                                        packet->data_type);
}

static uint8_t network_reliability_class(const xgl_packet_t* packet) {
    if (packet->reliable == XGL_RELIABILITY_ACK_ONLY) {
        return XGL_RELIABILITY_ACK_ONLY;
    }
    if (packet->reliable != 0U) {
        return XGL_RELIABILITY_ACK_ELICITING;
    }
    return XGL_RELIABILITY_NONE;
}

static xgl_error_t network_build_tx_frame(xgl_network_ctx_t* ctx,
                                          const xgl_packet_t* packet,
                                          uint8_t* extensions,
                                          size_t extensions_capacity,
                                          xgl_frame_t* frame) {
    size_t extensions_len = 0U;
    xgl_error_t err = network_copy_packet_extensions(extensions,
                                                     extensions_capacity,
                                                     &extensions_len,
                                                     packet);
    if (err != XGL_OK) {
        network_count_tx_error(ctx);
        return err;
    }

    xgl_frame_params_t params = {
        .source_id = packet->source_id,
        .target_id = packet->target_id,
        .data_type = packet->data_type,
        .packet_type = packet->packet_type,
        .flags = packet->flags,
        .traffic_class = packet->traffic_class,
        .connection_id = packet->connection_id,
        .packet_number = packet->packet_number,
        .session_epoch = packet->session_epoch,
        .extensions = (extensions_len > 0U) ? extensions : NULL,
        .extensions_len = extensions_len,
        .payload = packet->data->data,
        .payload_len = packet->data->data_len,
        .reliable = packet->reliable,
        .reliability_class = network_reliability_class(packet),
        .fragment = packet->fragment,
        .priority = packet->priority,
        .session_id = packet->session_id,
        .ttl = XGL_DEFAULT_TTL
    };

    err = xgl_frame_build(frame, &params);
    if (err != XGL_OK) {
        network_count_tx_error(ctx);
    }
    return err;
}

static xgl_error_t network_validate_auth_tx_budget(xgl_network_ctx_t* ctx,
                                                   const xgl_frame_t* frame,
                                                   const xgl_route_item_t* route) {
    size_t auth_tag_len = 0U;
    if (ctx->auth_required) {
        if (ctx->auth_provider == NULL ||
            ctx->auth_provider->sign == NULL ||
            ctx->auth_provider->tag_len == 0U ||
            ctx->auth_provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
            network_count_tx_error(ctx);
            return XGL_ERR_INVALID_PARAM;
        }
        auth_tag_len = ctx->auth_provider->tag_len;
    }

    if (xgl_frame_serialized_size(frame->payload_len,
                                  frame->extensions_len,
                                  auth_tag_len) > route->max_frame_size) {
        network_count_tx_error(ctx);
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    return XGL_OK;
}

static xgl_error_t network_send_frame_to_lower(xgl_network_ctx_t* ctx,
                                               xgl_handle_t handle,
                                               const xgl_packet_t* packet,
                                               xgl_frame_t* frame) {
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        network_count_tx_error(ctx);
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_frame_tx_message_t send_data = {
        .frame = frame,
        .phy = packet->phy
    };

    xgl_error_t err = ctx->lower_layer->send(ctx->lower_layer->ctx,
                                            handle,
                                            &send_data);
    if (err != XGL_OK) {
        network_count_tx_error(ctx);
    }
    return err;
}

xgl_error_t xgl_network_send_with_handle(xgl_network_ctx_t* ctx,
                                         xgl_handle_t handle,
                                         xgl_packet_t* packet,
                                         bool assign_packet_number) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (packet->data == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_route_item_t* route = xgl_route_table_lookup(ctx->route_table,
                                                     packet->target_id);
    if (route == NULL) {
        char error_msg[64];
        snprintf(error_msg,
                 sizeof(error_msg),
                 "Route not found for target ID: %u",
                 (unsigned int)packet->target_id);

        if (ctx->error_callback != NULL) {
            ctx->error_callback(handle,
                                XGL_ERR_ROUTE_NOT_FOUND,
                                error_msg,
                                ctx->callback_user_data);
        }

        network_count_tx_error(ctx);
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    packet->phy = route->phy;
    if (packet->source_id == 0U) {
        packet->source_id = ctx->local_id;
    }
    packet->version = XGL_PROTOCOL_VERSION;
    (void)assign_packet_number;

    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += packet->data->data_len;
    }

    uint8_t extensions[UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE] = {0};
    xgl_frame_t frame;
    xgl_error_t err = network_build_tx_frame(ctx,
                                             packet,
                                             extensions,
                                             sizeof(extensions),
                                             &frame);
    if (err != XGL_OK) {
        return err;
    }

    err = network_validate_auth_tx_budget(ctx, &frame, route);
    if (err != XGL_OK) {
        return err;
    }

    return network_send_frame_to_lower(ctx, handle, packet, &frame);
}

xgl_error_t xgl_network_send(xgl_network_ctx_t* ctx,
                             xgl_packet_t* packet,
                             bool assign_packet_number) {
    return xgl_network_send_with_handle(ctx, NULL, packet, assign_packet_number);
}
