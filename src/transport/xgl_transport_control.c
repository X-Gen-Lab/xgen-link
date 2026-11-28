/**
 * \file            xgl_transport_control.c
 * \brief           Transport control packet helpers
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-15
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "wire/xgl_wire.h"
#include "xgl_transport_internal.h"

/**
 * \brief           Send a control request without consuming a reliable DATA
 * number
 * \details         Return the lower layer result for a HELLO or RESET within
 *                  the exact peer scope.
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
                           .priority = 7,
                           .data = &packet_data,
                           .extensions = control_ext,
                           .extensions_len = control_ext_len};

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    return transport_submit_packet(ctx, handle, &packet);
}

/**
 * \brief           Ensure HELLO ownership or terminally cancel an exact RESET
 * scope
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
    /* A duplicate HELLO cannot keep an unused reservation alive forever.
     * Accepted DATA, owned work and failed identities retain their normal
     * explicit lifecycle instead of becoming lease-reclaimable. */
    if (!peer->hello_reserved && transport_peer_can_reclaim(peer)) {
        peer->hello_reserved = true;
        peer->hello_reserved_ms = transport_now(ctx);
    }
    return XGL_OK;
}
