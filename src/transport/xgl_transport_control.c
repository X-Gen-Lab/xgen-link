/**
 * \file            xgl_transport_control.c
 * \brief           Transport control packet helpers
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

/**
 * \brief           Send a control request without consuming a reliable DATA
 * number
 * \param[in]       ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       target_id: Remote endpoint
 * \param[in]       control_type: HELLO or RESET operation
 * \param[in]       connection_id: Exact connection identity
 * \param[in]       session_epoch: Exact session epoch
 * \return          Lower layer send result
 */
xgl_error_t transport_send_control(const xgl_transport_ctx_t* ctx,
                                   xgl_handle_t handle, uint16_t target_id,
                                   uint8_t control_type, uint32_t connection_id,
                                   uint32_t session_epoch) {
    xgl_packet_data_t packet_data = {.data_len = 0, .data = NULL};
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
                           .connection_id = connection_id,
                           .packet_number = 0U,
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

    return xgl_packet_send(ctx->lower_layer, handle, &packet);
}

/**
 * \brief           Ensure HELLO ownership or terminally cancel an exact RESET
 * scope
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Borrowed control request
 * \return          XGL_OK, or validation/capacity/unknown-scope error
 * \note            Repeated HELLO and RESET are idempotent. RESET never reopens
 *                  a failed epoch and never creates a previously unknown peer.
 */
xgl_error_t transport_process_control_packet(xgl_transport_ctx_t* ctx,
                                             xgl_handle_t handle,
                                             const xgl_packet_t* packet) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (packet->packet_type != XGL_PACKET_TYPE_CONTROL ||
        (packet->data_type != XGL_TRANSPORT_CONTROL_HELLO &&
         packet->data_type != XGL_TRANSPORT_CONTROL_RESET)) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (packet->data_type == XGL_TRANSPORT_CONTROL_RESET) {
        xgl_transport_peer_state_t* peer = transport_find_peer_scope(
            ctx, packet->source_id, packet->connection_id,
            packet->session_epoch);
        if (peer == NULL) {
            return XGL_ERR_NOT_FOUND;
        }
        /* A reset is terminal for this exact epoch. Repeated resets neither
         * report twice nor reopen old packet numbers. */
        transport_fail_peer(ctx, handle, peer, XGL_ERR_CANCELLED);
        return XGL_OK;
    }

    xgl_transport_peer_state_t* peer = transport_get_or_create_peer_scope(
        ctx, packet->source_id, packet->connection_id, packet->session_epoch);
    if (peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    peer->last_active_ms = transport_now(ctx);
    return XGL_OK;
}
