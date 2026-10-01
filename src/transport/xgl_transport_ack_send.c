/**
 * \file            xgl_transport_ack_send.c
 * \brief           ACK and selective acknowledgement emission
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

/**
 * \brief           Submit a borrowed ACK extension with a shared packet header
 * \param[in]       ctx: Transport context
 * \param[in]       handle: Protocol instance
 * \param[in]       source_id: Remote endpoint being acknowledged
 * \param[in]       connection_id: Exact connection identity
 * \param[in]       session_epoch: Exact session epoch
 * \param[in]       extension: Borrowed encoded acknowledgement
 * \param[in]       extension_len: Encoded extension size
 * \return          Synchronous lower-layer submission result
 */
/* Parameter order follows the exact peer identity and byte-span contract. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
static xgl_error_t transport_emit_ack(const xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle, uint16_t source_id,
                                      uint32_t connection_id,
                                      uint32_t session_epoch,
                                      uint8_t* extension,
                                      size_t extension_len) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    xgl_packet_data_t payload = {.data_len = 0U, .data = NULL};
    xgl_packet_t packet = {.source_id = ctx->local_id,
                           .target_id = source_id,
                           .connection_id = connection_id,
                           .packet_number = 0U,
                           .session_epoch = session_epoch,
                           .packet_type = XGL_PACKET_TYPE_ACK,
                           .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
                           .reliable = XGL_RELIABILITY_ACK_ONLY,
                           .priority = 7,
                           .data = &payload,
                           .extensions = extension,
                           .extensions_len = extension_len};
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    return xgl_packet_send(ctx->lower_layer, handle, &packet);
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Send ACK packet for received data
 * \param[in]       ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet_number: Packet number to acknowledge
 * \param[in]       source_id: Source node ID
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t transport_send_ack(const xgl_transport_ctx_t* ctx,
                               xgl_handle_t handle, uint32_t packet_number,
                               uint16_t source_id, uint32_t connection_id,
                               uint32_t session_epoch) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    uint8_t ack_value[16] = {0};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {{.gap = 0, .length = 1}};

    xgl_error_t err = xgl_wire_encode_ack_range_ext_value(
        ack_value, sizeof(ack_value), packet_number, 0, ranges, 1,
        &ack_value_len);
    if (err != XGL_OK) {
        return err;
    }

    uint8_t ack_ext[32] = {0};
    size_t ack_ext_len = 0;
    err = xgl_wire_encode_ext(ack_ext, sizeof(ack_ext), XGL_WIRE_EXT_ACK_RANGE,
                              ack_value, ack_value_len, &ack_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    return transport_emit_ack(ctx, handle, source_id, connection_id,
                              session_epoch, ack_ext, ack_ext_len);
}

#if XGL_FEATURE_OUT_OF_ORDER
/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t transport_send_sack(const xgl_transport_ctx_t* ctx,
                                xgl_handle_t handle,
                                const xgl_transport_peer_state_t* peer,
                                uint16_t source_id, uint32_t base_packet,
                                uint32_t connection_id,
                                uint32_t session_epoch) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint8_t bitmap[8] = {0};
    size_t highest_bit = 0U;
    bool has_received = false;
    for (const xgl_transport_rx_buffered_packet_t* node = peer->rx_buffered;
         node != NULL; node = node->next) {
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
    xgl_error_t err = xgl_wire_encode_sack_ext_value(
        sack_value, sizeof(sack_value), base_packet, bitmap, bitmap_len,
        &sack_value_len);
    if (err != XGL_OK) {
        return err;
    }

    uint8_t sack_ext[32] = {0};
    size_t sack_ext_len = 0U;
    err = xgl_wire_encode_ext(sack_ext, sizeof(sack_ext), XGL_WIRE_EXT_SACK,
                              sack_value, sack_value_len, &sack_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    return transport_emit_ack(ctx, handle, source_id, connection_id,
                              session_epoch, sack_ext, sack_ext_len);
}
#endif
