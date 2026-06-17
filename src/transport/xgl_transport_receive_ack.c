/**
 * \file            xgl_transport_receive_ack.c
 * \brief           Transport ACK receive dispatch
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

static xgl_transport_peer_state_t *
transport_find_ack_peer(xgl_transport_ctx_t *ctx, const xgl_packet_t *packet)
{
    bool scoped = (packet->connection_id != 0U || packet->session_epoch != 0U);
    xgl_transport_peer_state_t *peer =
        scoped ? transport_find_peer_scope(ctx, packet->source_id,
                                           packet->connection_id,
                                           packet->session_epoch)
               : transport_find_peer(ctx, packet->source_id);
    if (peer == NULL && scoped) {
        peer = transport_find_peer(ctx, packet->source_id);
    }

    return peer;
}

xgl_error_t transport_process_ack_packet(xgl_transport_ctx_t *ctx,
                                         xgl_handle_t handle,
                                         const xgl_packet_t *packet)
{
    if (packet->packet_type != XGL_PACKET_TYPE_ACK) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_transport_peer_state_t *peer = transport_find_ack_peer(ctx, packet);
    if (peer == NULL) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    if (packet->session_id != 0U && packet->session_id != peer->session_id) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    if ((packet->flags & XGL_WIRE_FLAG_HAS_EXTENSIONS) == 0U ||
        packet->extensions == NULL || packet->extensions_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    bool handled_ack_range = false;
    xgl_error_t err = transport_try_process_ack_range_ext(
        ctx, peer, packet->source_id, packet->extensions,
        packet->extensions_len, &handled_ack_range);
    if (err != XGL_OK || handled_ack_range) {
        return err;
    }

    bool handled_sack = false;
    err = transport_try_process_sack_ext(ctx, handle, peer, packet->source_id,
                                         packet->extensions,
                                         packet->extensions_len, &handled_sack);
    if (err != XGL_OK || handled_sack) {
        return err;
    }

    return XGL_ERR_INVALID_FRAME;
}
