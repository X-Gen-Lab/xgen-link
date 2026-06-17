/**
 * \file            xgl_transport_sack_send.c
 * \brief           Transport SACK send helpers
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

xgl_error_t transport_send_sack(const xgl_transport_ctx_t *ctx,
                                xgl_handle_t handle,
                                const xgl_transport_peer_state_t *peer,
                                uint16_t source_id, uint32_t base_packet,
                                uint16_t session_id, uint32_t connection_id,
                                uint32_t session_epoch)
{
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint8_t bitmap[8] = {0};
    size_t highest_bit = 0U;
    bool has_received = false;
    for (const xgl_transport_rx_buffered_packet_t *node = peer->rx_buffered;
         node != NULL; node = node->next) {
        if (node->packet.packet_number < base_packet) {
            continue;
        }
        uint32_t diff = node->packet.packet_number - base_packet;
        if (diff >= (uint32_t) (sizeof(bitmap) * 8U)) {
            continue;
        }
        bitmap[diff / 8U] |= (uint8_t) (1U << (diff % 8U));
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

    xgl_packet_data_t sack_packet_data = {
        .ref_count = 1, .data_len = 0, .data = NULL, .owned_data = NULL};

    xgl_packet_t sack_packet = {.source_id = ctx->local_id,
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
                                .phy = NULL};

    if (ctx->lower_layer != NULL && ctx->lower_layer->send != NULL) {
        return xgl_layer_send(ctx->lower_layer, handle, &sack_packet);
    }

    return XGL_ERR_INVALID_PARAM;
}
