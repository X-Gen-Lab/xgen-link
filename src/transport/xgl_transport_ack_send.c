/**
 * \file            xgl_transport_ack_send.c
 * \brief           Transport ACK send helpers
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

/**
 * \brief           Send ACK packet for received data
 * \param[in]       ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet_number: Packet number to acknowledge
 * \param[in]       source_id: Source node ID
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t transport_send_ack(const xgl_transport_ctx_t *ctx,
                               xgl_handle_t handle, uint32_t packet_number,
                               uint16_t source_id, uint16_t session_id,
                               uint32_t connection_id, uint32_t session_epoch)
{
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

    xgl_packet_data_t ack_packet_data = {
        .ref_count = 1, .data_len = 0, .data = NULL, .owned_data = NULL};

    xgl_packet_t ack_packet = {.source_id = ctx->local_id,
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
                               .phy = NULL};

    if (ctx->lower_layer != NULL && ctx->lower_layer->send != NULL) {
        return xgl_layer_send(ctx->lower_layer, handle, &ack_packet);
    }

    return XGL_ERR_INVALID_PARAM;
}
