/**
 * \file            xgl_network_send.c
 * \brief           Network send path implementation
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-16
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <internal/xgl_diagnostics.h>
#include <string.h>

#include "network/xgl_route.h"
#include "wire/xgl_frame.h"
#include "wire/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl_network_internal.h"

/* Transport emits at most 15 bytes of feedback plus a SESSION and optional
 * DATA_TYPE. Only the generic network sender serves caller-supplied TLVs. */
#define XGL_NETWORK_TX_GENERATED_EXT_CAPACITY 32U
#if XGL_BUILD_PROFILE_BOOT
#define XGL_NETWORK_TX_GENERIC_EXT_CAPACITY                                    \
    XGL_NETWORK_TX_GENERATED_EXT_CAPACITY
#else
#define XGL_NETWORK_TX_GENERIC_EXT_CAPACITY                                    \
    (UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE)
#endif

static void network_count_tx_error(xgl_network_ctx_t* ctx) {
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
#else
    (void)ctx;
#endif
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
    xgl_error_t err = xgl_wire_encode_ext(
        &extensions[*extensions_len], extensions_capacity - *extensions_len,
        XGL_WIRE_EXT_DATA_TYPE, &data_type, 1U, &bytes_written);
    if (err != XGL_OK) {
        return err;
    }
    *extensions_len += bytes_written;
    return XGL_OK;
}

/**
 * \brief           Compose packet extensions while preserving session identity
 * \details         Compose into the caller buffer and report the resulting
 *                  byte count within its capacity.
 */
static xgl_error_t network_copy_packet_extensions(uint8_t* extensions,
                                                  size_t extensions_capacity,
                                                  size_t* extensions_len,
                                                  const xgl_packet_t* packet) {
    if (extensions == NULL || extensions_len == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *extensions_len = 0U;
    if (packet->extensions == NULL && packet->extensions_len > 0U) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_wire_ext_metadata_t metadata = {0};
    xgl_error_t err;
    if (packet->extensions_len > 0U) {
        if (packet->extensions_len > extensions_capacity) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        err = xgl_wire_decode_ext_metadata(
            packet->extensions, packet->extensions_len, false, &metadata);
        if (err != XGL_OK) {
            return err;
        }
        memcpy(extensions, packet->extensions, packet->extensions_len);
        *extensions_len = packet->extensions_len;
    }

    if (metadata.data_type_offset != 0U) {
        if (packet->data_type != 0U &&
            packet->data_type != metadata.data_type) {
            return XGL_ERR_INVALID_PARAM;
        }
    } else {
        err = network_append_data_type_ext(extensions, extensions_capacity,
                                           extensions_len, packet->data_type);
        if (err != XGL_OK) {
            return err;
        }
    }
    if (metadata.session_offset != 0U) {
        return metadata.session_epoch == packet->session_epoch
                   ? XGL_OK
                   : XGL_ERR_INVALID_PARAM;
    }
    if (packet->session_epoch == 0U) {
        return XGL_OK;
    }
    uint8_t session_value[XGL_SESSION_EXT_VALUE_SIZE];
    size_t value_len = 0U;
    err = xgl_wire_encode_session_ext_value(
        session_value, sizeof(session_value), packet->session_epoch, 0U,
        &value_len);
    if (err != XGL_OK) {
        return err;
    }
    size_t written = 0U;
    err = xgl_wire_encode_ext(
        extensions + *extensions_len, extensions_capacity - *extensions_len,
        XGL_WIRE_EXT_SESSION, session_value, value_len, &written);
    if (err == XGL_OK) {
        *extensions_len += written;
    }
    return err;
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

/**
 * \brief           Build a frame using composed extensions and wire scope
 *                  fields
 * \details         The result borrows the payload and caller-owned extension
 *                  storage.
 */
static xgl_error_t network_build_tx_frame(xgl_network_ctx_t* ctx,
                                          const xgl_packet_t* packet,
                                          uint8_t* extensions,
                                          size_t extensions_capacity,
                                          xgl_frame_t* frame) {
    size_t extensions_len = 0U;
    xgl_error_t err = network_copy_packet_extensions(
        extensions, extensions_capacity, &extensions_len, packet);
    if (err != XGL_OK) {
        network_count_tx_error(ctx);
        return err;
    }

    xgl_frame_params_t params = {
        .source_id = packet->source_id,
        .target_id = packet->target_id,
        .packet_type = packet->packet_type,
        .flags = packet->flags,
        .connection_id = packet->connection_id,
        .packet_number = packet->packet_number,
        .extensions = (extensions_len > 0U) ? extensions : NULL,
        .extensions_len = extensions_len,
        .payload = packet->data->data,
        .payload_len = packet->data->data_len,
        .reliable = packet->reliable,
        .reliability_class = network_reliability_class(packet),
        .priority = packet->priority,
        .ttl = XGL_DEFAULT_TTL};

    err = xgl_frame_build(frame, &params);
    if (err != XGL_OK) {
        network_count_tx_error(ctx);
    }
    return err;
}

/* Lengths follow the wire layout and use the same unit. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
static xgl_error_t network_measure_egress_frame(const xgl_network_ctx_t* ctx,
                                                const xgl_route_item_t* route,
                                                size_t payload_len,
                                                size_t extensions_len,
                                                size_t tag_len,
                                                xgl_frame_layout_t* layout) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    xgl_error_t error =
        xgl_frame_measure_lengths(payload_len, extensions_len, tag_len, layout);
    if (error != XGL_OK) {
        return error;
    }
    const size_t instance_limit =
        ctx->max_frame_size != 0U &&
                ctx->max_frame_size < XGL_DATALINK_MAX_FRAME_SIZE
            ? ctx->max_frame_size
            : XGL_DATALINK_MAX_FRAME_SIZE;
    return layout->frame_len <= xgl_route_frame_limit(route, instance_limit)
               ? XGL_OK
               : XGL_ERR_BUFFER_TOO_SMALL;
}

/* Parameter order follows the documented protocol scope. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
static xgl_error_t
network_check_scope_tag(const xgl_network_ctx_t* ctx, uint16_t target_id,
                        uint32_t connection_id, uint32_t session_epoch,
                        uint8_t flags, bool need_second_signature,
                        size_t* tag_len) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
#if XGL_FEATURE_AUTH
    const xgl_security_ctx_t* security = ctx->security;
    xgl_error_t error = xgl_security_frame_tag_len(
        xgl_security_scope_requires_auth(security, target_id, connection_id,
                                         session_epoch),
        security != NULL ? security->provider : NULL, flags, tag_len);
    if (error != XGL_OK || *tag_len == 0U) {
        return error;
    }
    return xgl_security_check_tx_scope(security, target_id, connection_id,
                                       session_epoch, need_second_signature);
#else
    (void)ctx;
    (void)target_id;
    (void)connection_id;
    (void)session_epoch;
    (void)need_second_signature;
    *tag_len = 0U;
    return (flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U ? XGL_ERR_INVALID_FRAME
                                                       : XGL_OK;
#endif
}

/* Caller-supplied extension lengths exclude SECURITY. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
static xgl_error_t
network_prepare_egress(const xgl_network_ctx_t* ctx,
                       const xgl_route_item_t* route, uint32_t connection_id,
                       uint32_t session_epoch, uint8_t flags,
                       size_t payload_len, size_t extensions_len,
                       bool need_second_signature, xgl_frame_layout_t* layout) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    size_t tag_len;
    xgl_error_t error = network_check_scope_tag(
        ctx, route->target_id, connection_id, session_epoch, flags,
        need_second_signature, &tag_len);
    if (error != XGL_OK) {
        return error;
    }
    error = network_measure_egress_frame(ctx, route, payload_len,
                                         extensions_len, tag_len, layout);
    if (error == XGL_OK) {
        layout->session_epoch = session_epoch;
    }
    return error;
}

static void network_report_missing_route(xgl_network_ctx_t* ctx,
                                         xgl_handle_t handle) {
    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle, XGL_ERR_ROUTE_NOT_FOUND,
                            XGL_ERROR_MESSAGE("Route not found"),
                            ctx->callback_user_data);
    }
    network_count_tx_error(ctx);
}

xgl_error_t xgl_network_check_data(void* network, xgl_handle_t handle,
                                   const xgl_tx_data_t* data, bool need_hello) {
    xgl_network_ctx_t* ctx = network;
    const xgl_route_item_t* route =
        xgl_route_table_lookup(ctx->route_table, data->target_id);
    if (route == NULL) {
        network_report_missing_route(ctx, handle);
        return XGL_ERR_ROUTE_NOT_FOUND;
    }
    if (route->phy == NULL || route->phy->tx == NULL ||
        ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        network_count_tx_error(ctx);
        return XGL_ERR_INVALID_PARAM;
    }
    const size_t extensions_len =
        xgl_wire_packet_extension_size(data->data_type, data->session_epoch);
    xgl_frame_layout_t layout;
    xgl_error_t error = network_prepare_egress(
        ctx, route, data->connection_id, data->session_epoch, 0U,
        data->data_len, extensions_len, need_hello, &layout);
    if (error == XGL_OK && need_hello) {
        /* HELLO adds a DATA_TYPE TLV even if DATA has no type or one byte. */
        const size_t hello_extensions_len =
            XGL_DATA_TYPE_EXT_SIZE +
            xgl_wire_packet_extension_size(0U, data->session_epoch);
        error = network_measure_egress_frame(
            ctx, route, 0U, hello_extensions_len, layout.tag_len, &layout);
    }
    return error;
}

xgl_error_t xgl_network_check_feedback(void* network,
                                       const xgl_packet_t* received,
                                       size_t ack_extension_len) {
    const xgl_network_ctx_t* ctx = network;
    const xgl_route_item_t* route =
        xgl_route_table_lookup(ctx->route_table, received->source_id);
    if (route == NULL) {
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    const size_t extensions_len =
        ack_extension_len +
        xgl_wire_packet_extension_size(0U, received->session_epoch);
    xgl_frame_layout_t layout;
    return network_prepare_egress(ctx, route, received->connection_id,
                                  received->session_epoch, 0U, 0U,
                                  extensions_len, false, &layout);
}

static xgl_error_t
network_send_frame_to_lower(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                            xgl_phy_ops_t* phy, xgl_frame_t* frame,
                            const xgl_frame_layout_t* layout,
                            const xgl_tx_data_zerocopy_t* inplace) {
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        network_count_tx_error(ctx);
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_frame_tx_message_t send_data = {
        .frame = frame, .layout = layout, .phy = phy};
    if (inplace != NULL) {
        send_data.buffer = inplace->buffer;
        send_data.buffer_size = inplace->buffer_size;
        send_data.payload_offset = inplace->data_offset;
    }

    xgl_error_t err =
        ctx->lower_layer->send(ctx->lower_layer->ctx, handle, &send_data);
    if (err != XGL_OK) {
        network_count_tx_error(ctx);
    }
    return err;
}

static xgl_error_t
network_send_packet(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                    xgl_packet_t* packet, const xgl_tx_data_zerocopy_t* inplace,
                    uint8_t* extensions, size_t extensions_capacity) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (packet->data == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (packet->data->data_len != 0U && packet->data->data == NULL) {
        network_count_tx_error(ctx);
        return XGL_ERR_NULL_POINTER;
    }

    xgl_route_item_t* route =
        xgl_route_table_lookup(ctx->route_table, packet->target_id);
    if (route == NULL) {
        network_report_missing_route(ctx, handle);
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    if (packet->source_id == 0U) {
        packet->source_id = ctx->local_id;
    }

    xgl_frame_t frame;
    xgl_error_t err = network_build_tx_frame(ctx, packet, extensions,
                                             extensions_capacity, &frame);
    if (err != XGL_OK) {
        return err;
    }

    xgl_frame_layout_t layout;
    err = network_prepare_egress(
        ctx, route, packet->connection_id, packet->session_epoch, packet->flags,
        frame.payload_len, frame.extensions_len, false, &layout);
    if (err != XGL_OK) {
        network_count_tx_error(ctx);
        return err;
    }
    if (layout.tag_len != 0U) {
        frame.header.flags |= XGL_WIRE_FLAG_AUTHENTICATED;
    }

    err = network_send_frame_to_lower(ctx, handle, route->phy, &frame, &layout,
                                      inplace);
#if XGL_FEATURE_STATISTICS
    if (err == XGL_OK && ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += packet->data->data_len;
    }
#endif
    return err;
}

xgl_error_t xgl_network_send_generated(xgl_network_ctx_t* ctx,
                                       xgl_handle_t handle,
                                       xgl_packet_t* packet) {
    uint8_t extensions[XGL_NETWORK_TX_GENERATED_EXT_CAPACITY];
    return network_send_packet(ctx, handle, packet, NULL, extensions,
                               sizeof(extensions));
}

xgl_error_t xgl_network_send(xgl_network_ctx_t* ctx, xgl_packet_t* packet) {
    uint8_t extensions[XGL_NETWORK_TX_GENERIC_EXT_CAPACITY];
    return network_send_packet(ctx, NULL, packet, NULL, extensions,
                               sizeof(extensions));
}

/**
 * \brief           Validate zero-copy transmission data
 */
xgl_error_t
xgl_network_validate_tx_data_zerocopy(const xgl_tx_data_zerocopy_t* tx_data) {
    if (tx_data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tx_data->buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tx_data->data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (tx_data->data_offset < XGL_FRAME_HEADER_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (tx_data->data_offset > tx_data->buffer_size ||
        tx_data->data_len > tx_data->buffer_size - tx_data->data_offset ||
        XGL_CRC16_SIZE >
            tx_data->buffer_size - tx_data->data_offset - tx_data->data_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (tx_data->priority > 7) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (tx_data->reliable) {
        return XGL_ERR_INVALID_PARAM;
    }

    return XGL_OK;
}

/**
 * \brief           Apply shared routing and framing to caller-owned TX storage
 */
xgl_error_t
xgl_network_send_zerocopy_scope(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const xgl_tx_data_zerocopy_t* request,
                                uint32_t connection_id, uint32_t session_epoch,
                                bool* attempted) {
    if (attempted != NULL) {
        *attempted = false;
    }
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_error_t error = xgl_network_validate_tx_data_zerocopy(request);
    if (error != XGL_OK) {
        return error;
    }
    if (attempted != NULL) {
        *attempted = true;
    }
    xgl_packet_data_t data = {
        .data = request->buffer + request->data_offset,
        .data_len = request->data_len,
    };
    xgl_packet_t packet = {
        .source_id = ctx->local_id,
        .target_id = request->target_id,
        .connection_id = connection_id,
        .session_epoch = session_epoch,
        .data_type = request->data_type,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .priority = request->priority,
        .data = &data,
    };
    uint8_t extensions[XGL_NETWORK_TX_GENERATED_EXT_CAPACITY];
    return network_send_packet(ctx, handle, &packet, request, extensions,
                               sizeof(extensions));
}

xgl_error_t xgl_network_send_zerocopy(xgl_network_ctx_t* ctx,
                                      xgl_handle_t handle,
                                      const xgl_tx_data_zerocopy_t* request) {
    return xgl_network_send_zerocopy_scope(ctx, handle, request, 0U, 0U, NULL);
}

xgl_error_t
xgl_network_send_zerocopy_scoped(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                 const xgl_tx_zerocopy_request_t* request) {
    if (request == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return xgl_network_send_zerocopy_scope(ctx, handle, &request->data,
                                           request->connection_id,
                                           request->session_epoch, NULL);
}

xgl_error_t
xgl_network_zerocopy_layout(xgl_network_ctx_t* ctx,
                            const xgl_tx_zerocopy_request_t* request,
                            xgl_tx_frame_layout_t* layout) {
    if (layout == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(layout, 0, sizeof(*layout));
    if (ctx == NULL || request == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    const xgl_tx_data_zerocopy_t* data = &request->data;
    if (data->data_len == 0U || data->priority > 7U || data->reliable ||
        data->target_id == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (data->data_len > UINT16_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    const xgl_route_item_t* route =
        xgl_route_table_lookup(ctx->route_table, data->target_id);
    if (route == NULL) {
        return XGL_ERR_ROUTE_NOT_FOUND;
    }
    size_t extensions_len =
        xgl_wire_packet_extension_size(data->data_type, request->session_epoch);
    xgl_frame_layout_t measured;
    xgl_error_t error_layout = network_prepare_egress(
        ctx, route, request->connection_id, request->session_epoch, 0U,
        data->data_len, extensions_len, false, &measured);
    if (error_layout != XGL_OK) {
        return error_layout;
    }
    layout->payload_offset = measured.header_len;
    layout->frame_size = measured.frame_len;
    return XGL_OK;
}
