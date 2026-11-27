/**
 * \file            xgl_network_send.c
 * \brief           Network send path implementation
 */

#include <internal/xgl_diagnostics.h>
#include <string.h>

#include "network/xgl_route.h"
#include "wire/xgl_frame.h"
#include "wire/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl_network_internal.h"

/* Boot emits one ACK range, SESSION and optional DATA_TYPE. This logical TX
 * workspace does not constrain the wire decoder or serialized frame ingress. */
#if !XGL_FEATURE_AUTH && !XGL_FEATURE_FORWARDING &&                            \
    !XGL_FEATURE_FRAGMENTATION && !XGL_FEATURE_OUT_OF_ORDER
#define XGL_NETWORK_TX_EXT_CAPACITY 32U
#else
#define XGL_NETWORK_TX_EXT_CAPACITY (UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE)
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
 * \param[out]      extensions: Destination extension buffer
 * \param[in]       extensions_capacity: Destination capacity in bytes
 * \param[out]      extensions_len: Composed extension length
 * \param[in]       packet: Packet metadata and optional existing extensions
 * \return          XGL_OK on success, error code otherwise
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
    if (packet->extensions_len > 0U) {
        if (packet->extensions_len > extensions_capacity) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(extensions, packet->extensions, packet->extensions_len);
        *extensions_len = packet->extensions_len;
    }

    xgl_wire_ext_metadata_t metadata;
    xgl_error_t err =
        xgl_wire_decode_ext_metadata(extensions, *extensions_len, &metadata);
    if (err != XGL_OK) {
        return err;
    }
    if (metadata.data_type_found) {
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
    if (metadata.session_epoch_found) {
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
 * \param[in,out]   ctx: Network statistics context
 * \param[in]       packet: Packet to transmit
 * \param[out]      extensions: Caller-owned extension workspace
 * \param[in]       extensions_capacity: Extension workspace capacity in bytes
 * \param[out]      frame: Built frame borrowing payload and extension storage
 * \return          XGL_OK on success, error code otherwise
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
        .traffic_class = packet->traffic_class,
        .connection_id = packet->connection_id,
        .packet_number = packet->packet_number,
        .extensions = (extensions_len > 0U) ? extensions : NULL,
        .extensions_len = extensions_len,
        .payload = packet->data->data,
        .payload_len = packet->data->data_len,
        .reliable = packet->reliable,
        .reliability_class = network_reliability_class(packet),
        .fragment = packet->fragment,
        .priority = packet->priority,
        .ttl = XGL_DEFAULT_TTL};

#if XGL_FEATURE_AUTH && XGL_FEATURE_ADVANCED_RUNTIME
    if (xgl_security_scope_requires_auth(ctx->security, packet->target_id,
                                         packet->connection_id,
                                         packet->session_epoch)) {
        params.flags |= XGL_WIRE_FLAG_AUTHENTICATED;
    }
#endif

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
        ctx->max_frame_size != 0U ? ctx->max_frame_size : SIZE_MAX;
    return layout->frame_len <= xgl_route_frame_limit(route, instance_limit)
               ? XGL_OK
               : XGL_ERR_BUFFER_TOO_SMALL;
}

#if XGL_FEATURE_AUTH
/* Parameter order follows the documented protocol scope. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
static xgl_error_t network_scope_tag_len(const xgl_network_ctx_t* ctx,
                                         uint16_t target_id,
                                         uint32_t connection_id,
                                         uint32_t session_epoch,
                                         size_t* tag_len) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    const xgl_security_ctx_t* security = ctx->security;
    return xgl_security_frame_tag_len(
        xgl_security_scope_requires_auth(security, target_id, connection_id,
                                         session_epoch),
        security != NULL ? security->provider : NULL, 0U, tag_len);
}
#endif

xgl_error_t xgl_network_check_feedback(void* network,
                                       const xgl_packet_t* received,
                                       size_t ack_extension_len) {
    const xgl_network_ctx_t* ctx = network;
    const xgl_route_item_t* route =
        xgl_route_table_lookup(ctx->route_table, received->source_id);
    if (route == NULL) {
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    size_t tag_len = 0U;
#if XGL_FEATURE_AUTH
    const xgl_error_t error =
        network_scope_tag_len(ctx, received->source_id, received->connection_id,
                              received->session_epoch, &tag_len);
    if (error != XGL_OK) {
        return error;
    }
#endif

    const size_t extensions_len =
        ack_extension_len +
        xgl_wire_packet_extension_size(0U, received->session_epoch);
    xgl_frame_layout_t layout;
    return network_measure_egress_frame(ctx, route, 0U, extensions_len, tag_len,
                                        &layout);
}

/**
 * \brief           Check the complete frame size against the egress route MTU
 * \param[in,out]   ctx: Network frame policy and statistics context
 * \param[in]       frame: Built frame including composed extensions
 * \param[in]       route: Selected egress route
 * \return          XGL_OK if the complete frame fits, error code otherwise
 */
static xgl_error_t network_validate_tx_budget(xgl_network_ctx_t* ctx,
                                              const xgl_frame_t* frame,
                                              const xgl_route_item_t* route) {
    size_t auth_tag_len = 0U;
#if XGL_FEATURE_AUTH
    const xgl_security_ctx_t* security = ctx->security;
    xgl_error_t error =
        xgl_security_frame_tag_len(security != NULL && security->auth_required,
                                   security != NULL ? security->provider : NULL,
                                   frame->header.flags, &auth_tag_len);
    if (error != XGL_OK) {
        network_count_tx_error(ctx);
        return error;
    }
#endif

    xgl_frame_layout_t layout;
    xgl_error_t measured = network_measure_egress_frame(
        ctx, route, frame->payload_len, frame->extensions_len, auth_tag_len,
        &layout);
    if (measured != XGL_OK) {
        network_count_tx_error(ctx);
    }
    return measured;
}

static xgl_error_t
network_send_frame_to_lower(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                            xgl_phy_ops_t* phy, xgl_frame_t* frame,
                            const xgl_tx_data_zerocopy_t* inplace) {
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        network_count_tx_error(ctx);
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_frame_tx_message_t send_data = {.frame = frame, .phy = phy};
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

static xgl_error_t network_send_packet(xgl_network_ctx_t* ctx,
                                       xgl_handle_t handle,
                                       xgl_packet_t* packet,
                                       const xgl_tx_data_zerocopy_t* inplace) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (packet->data == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_route_item_t* route =
        xgl_route_table_lookup(ctx->route_table, packet->target_id);
    if (route == NULL) {
        if (ctx->error_callback != NULL) {
            ctx->error_callback(handle, XGL_ERR_ROUTE_NOT_FOUND,
                                XGL_ERROR_MESSAGE("Route not found"),
                                ctx->callback_user_data);
        }

        network_count_tx_error(ctx);
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    if (packet->source_id == 0U) {
        packet->source_id = ctx->local_id;
    }

    uint8_t extensions[XGL_NETWORK_TX_EXT_CAPACITY];
    xgl_frame_t frame;
    xgl_error_t err = network_build_tx_frame(ctx, packet, extensions,
                                             sizeof(extensions), &frame);
    if (err != XGL_OK) {
        return err;
    }

    err = network_validate_tx_budget(ctx, &frame, route);
    if (err != XGL_OK) {
        return err;
    }

    err = network_send_frame_to_lower(ctx, handle, route->phy, &frame, inplace);
#if XGL_FEATURE_STATISTICS
    if (err == XGL_OK && ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += packet->data->data_len;
    }
#endif
    return err;
}

xgl_error_t xgl_network_send_with_handle(xgl_network_ctx_t* ctx,
                                         xgl_handle_t handle,
                                         xgl_packet_t* packet) {
    return network_send_packet(ctx, handle, packet, NULL);
}

xgl_error_t xgl_network_send(xgl_network_ctx_t* ctx, xgl_packet_t* packet) {
    return xgl_network_send_with_handle(ctx, NULL, packet);
}

/**
 * \brief           Validate zero-copy transmission data
 * \param[in]       tx_data: Borrowed caller buffer and send parameters
 * \return          XGL_OK when lengths and offsets are valid
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
    return network_send_packet(ctx, handle, &packet, request);
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
    size_t tag_len = 0U;
#if XGL_FEATURE_AUTH
    xgl_error_t error =
        network_scope_tag_len(ctx, data->target_id, request->connection_id,
                              request->session_epoch, &tag_len);
    if (error != XGL_OK) {
        return error;
    }
    if (tag_len != 0U) {
        error = xgl_security_check_tx_scope(ctx->security, data->target_id,
                                            request->connection_id,
                                            request->session_epoch);
        if (error != XGL_OK) {
            return error;
        }
    }
#endif
    size_t extensions_len =
        xgl_wire_packet_extension_size(data->data_type, request->session_epoch);
    xgl_frame_layout_t measured;
    xgl_error_t error_layout = network_measure_egress_frame(
        ctx, route, data->data_len, extensions_len, tag_len, &measured);
    if (error_layout != XGL_OK) {
        return error_layout;
    }
    layout->payload_offset = measured.header_len;
    layout->frame_size = measured.frame_len;
    return XGL_OK;
}
