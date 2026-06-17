/**
 * \file            xgl_transport_control.c
 * \brief           Transport control packet helpers
 */

#include "xgl/internal/xgl_time.h"
#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"
xgl_error_t transport_send_control(const xgl_transport_ctx_t *ctx,
                                   xgl_handle_t handle, uint16_t target_id,
                                   uint8_t control_type,
                                   uint32_t control_packet_number,
                                   uint16_t session_id, uint32_t connection_id,
                                   uint32_t session_epoch)
{
    xgl_packet_data_t packet_data = {
        .ref_count = 1, .data_len = 0, .data = NULL, .owned_data = NULL};
    uint8_t control_ext[4] = {0};
    size_t control_ext_len = 0U;
    xgl_error_t err = xgl_wire_encode_ext(control_ext, sizeof(control_ext),
                                          XGL_WIRE_EXT_DATA_TYPE, &control_type,
                                          1U, &control_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_packet_t packet = {.source_id = ctx->local_id,
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
                           .phy = NULL};

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    return xgl_layer_send(ctx->lower_layer, handle, &packet);
}

xgl_error_t transport_process_control_packet(xgl_transport_ctx_t *ctx,
                                             const xgl_packet_t *packet)
{
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint16_t session_id = (uint16_t) (packet->session_id & XGL_SESSION_ID_MASK);
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
    xgl_transport_peer_state_t *peer =
        has_connection_scope
            ? transport_get_or_create_peer_scope(ctx, packet->source_id,
                                                 packet->connection_id,
                                                 packet->session_epoch)
            : transport_get_or_create_peer(ctx, packet->source_id);
    if (peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    if (packet->data_type == XGL_TRANSPORT_CONTROL_RESET ||
        peer->session_id != session_id) {
        transport_reset_peer_state(ctx, peer, session_id, packet->connection_id,
                                   packet->session_epoch);
    } else {
        peer->session_established = true;
        peer->last_active_ms = xgl_time_ms();
    }

    return XGL_OK;
}
