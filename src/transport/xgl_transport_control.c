/**
 * \file            xgl_transport_control.c
 * \brief           Transport control packet helpers
 */

#include "xgl_transport_internal.h"
#include "xgl/internal/xgl_time.h"
#include "xgl/internal/xgl_wire.h"
xgl_error_t transport_send_control(const xgl_transport_ctx_t* ctx,
                                          xgl_handle_t handle,
                                          uint16_t target_id,
                                          uint8_t control_type,
                                          uint32_t control_packet_number,
                                          uint16_t session_id,
                                          uint32_t connection_id,
                                          uint32_t session_epoch) {
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = NULL,
        .owned_data = NULL
    };
    uint8_t control_ext[4] = {0};
    size_t control_ext_len = 0U;
    xgl_error_t err = xgl_wire_encode_ext(control_ext,
                                          sizeof(control_ext),
                                          XGL_WIRE_EXT_DATA_TYPE,
                                          &control_type,
                                          1U,
                                          &control_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_packet_t packet = {
        .source_id = ctx->local_id,
        .target_id = target_id,
        .session_id = session_id,
        .connection_id = connection_id,
        .packet_number = control_packet_number,
        .session_epoch = session_epoch,
        .packet_type = XGL_PACKET_TYPE_CONTROL,
        .data_type = control_type,
        .reliable = XGL_RELIABILITY_NONE,
        .fragment = false,
        .priority = 7,
        .data = &packet_data,
        .extensions = control_ext,
        .extensions_len = control_ext_len,
        .phy = NULL
    };

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    return xgl_layer_send(ctx->lower_layer, handle, &packet);
}

xgl_error_t transport_process_control_packet(xgl_transport_ctx_t* ctx,
                                                    const xgl_packet_t* packet) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint16_t session_id = (uint16_t)(packet->session_id & XGL_SESSION_ID_MASK);
    if (packet->packet_type != XGL_PACKET_TYPE_CONTROL ||
        (packet->data_type != XGL_TRANSPORT_CONTROL_HELLO &&
         packet->data_type != XGL_TRANSPORT_CONTROL_RESET)) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (session_id == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    bool has_connection_scope =
        (packet->connection_id != 0U || packet->session_epoch != 0U);
    xgl_transport_peer_state_t* peer = has_connection_scope ?
        transport_get_or_create_peer_scope(ctx,
                                           packet->source_id,
                                           packet->connection_id,
                                           packet->session_epoch) :
        transport_get_or_create_peer(ctx, packet->source_id);
    if (peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    if (packet->data_type == XGL_TRANSPORT_CONTROL_RESET ||
        peer->session_id != session_id) {
        transport_reset_peer_state(ctx,
                                   peer,
                                   session_id,
                                   packet->connection_id,
                                   packet->session_epoch);
    } else {
        peer->session_established = true;
        peer->last_active_ms = xgl_time_ms();
    }

    return XGL_OK;
}

xgl_error_t transport_send_sack(const xgl_transport_ctx_t* ctx,
                                xgl_handle_t handle,
                                const xgl_transport_peer_state_t* peer,
                                uint16_t source_id,
                                uint32_t base_packet,
                                uint16_t session_id,
                                uint32_t connection_id,
                                uint32_t session_epoch) {
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint8_t bitmap[8] = {0};
    size_t highest_bit = 0U;
    bool has_received = false;
    for (const xgl_transport_rx_buffered_packet_t* node = peer->rx_buffered;
         node != NULL;
         node = node->next) {
        if (node->packet.packet_number < base_packet) {
            continue;
        }
        uint32_t diff = node->packet.packet_number - base_packet;
        if (diff >= (uint32_t)(sizeof(bitmap) * 8U)) {
            continue;
        }
        bitmap[diff / 8U] |= (uint8_t)(1U << (diff % 8U));
        if (diff > highest_bit) {
            highest_bit = diff;
        }
        has_received = true;
    }

    size_t bitmap_len = has_received ? ((highest_bit / 8U) + 1U) : 1U;
    uint8_t sack_value[16] = {0};
    size_t sack_value_len = 0U;
    xgl_error_t err = xgl_wire_encode_sack_ext_value(sack_value,
                                                     sizeof(sack_value),
                                                     base_packet,
                                                     bitmap,
                                                     bitmap_len,
                                                     &sack_value_len);
    if (err != XGL_OK) {
        return err;
    }

    uint8_t sack_ext[32] = {0};
    size_t sack_ext_len = 0U;
    err = xgl_wire_encode_ext(sack_ext,
                              sizeof(sack_ext),
                              XGL_WIRE_EXT_SACK,
                              sack_value,
                              sack_value_len,
                              &sack_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_packet_data_t sack_packet_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = NULL,
        .owned_data = NULL
    };

    xgl_packet_t sack_packet = {
        .source_id = ctx->local_id,
        .target_id = source_id,
        .session_id = session_id,
        .connection_id = connection_id,
        .packet_number = base_packet,
        .session_epoch = session_epoch,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .fragment = false,
        .priority = 7,
        .data = &sack_packet_data,
        .extensions = sack_ext,
        .extensions_len = sack_ext_len,
        .phy = NULL
    };

    if (ctx->lower_layer != NULL && ctx->lower_layer->send != NULL) {
        return xgl_layer_send(ctx->lower_layer, handle, &sack_packet);
    }

    return XGL_ERR_INVALID_PARAM;
}

/**
 * \brief           Send ACK packet for received data
 * \param[in]       ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet_number: Packet number to acknowledge
 * \param[in]       source_id: Source node ID
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t transport_send_ack(const xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     uint32_t packet_number,
                                     uint16_t source_id,
                                     uint16_t session_id,
                                     uint32_t connection_id,
                                     uint32_t session_epoch) {
    uint8_t ack_value[16] = {0};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {
        {.gap = 0, .length = 1}
    };

    xgl_error_t err = xgl_wire_encode_ack_range_ext_value(ack_value,
                                                          sizeof(ack_value),
                                                          packet_number,
                                                          0,
                                                          ranges,
                                                          1,
                                                          &ack_value_len);
    if (err != XGL_OK) {
        return err;
    }

    uint8_t ack_ext[32] = {0};
    size_t ack_ext_len = 0;
    err = xgl_wire_encode_ext(ack_ext,
                              sizeof(ack_ext),
                              XGL_WIRE_EXT_ACK_RANGE,
                              ack_value,
                              ack_value_len,
                              &ack_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_packet_data_t ack_packet_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = NULL,
        .owned_data = NULL
    };

    xgl_packet_t ack_packet = {
        .source_id = ctx->local_id,
        .target_id = source_id,
        .session_id = session_id,
        .connection_id = connection_id,
        .packet_number = packet_number,
        .session_epoch = session_epoch,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .fragment = false,
        .priority = 7,
        .data = &ack_packet_data,
        .extensions = ack_ext,
        .extensions_len = ack_ext_len,
        .phy = NULL
    };

    if (ctx->lower_layer != NULL && ctx->lower_layer->send != NULL) {
        return xgl_layer_send(ctx->lower_layer, handle, &ack_packet);
    }

    return XGL_ERR_INVALID_PARAM;
}
