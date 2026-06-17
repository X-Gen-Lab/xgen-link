/**
 * \file            xgl_transport_receive_peer.c
 * \brief           Transport receive peer selection helpers
 */

#include "xgl_transport_internal.h"

xgl_transport_peer_state_t *transport_find_rx_peer(xgl_transport_ctx_t *ctx,
                                                   const xgl_packet_t *packet)
{
    bool scoped = (packet->connection_id != 0U || packet->session_epoch != 0U);
    if (scoped) {
        return transport_find_peer_scope(ctx, packet->source_id,
                                         packet->connection_id,
                                         packet->session_epoch);
    }

    return transport_find_peer(ctx, packet->source_id);
}

xgl_transport_peer_state_t *
transport_get_or_create_rx_peer(xgl_transport_ctx_t *ctx,
                                const xgl_packet_t *packet)
{
    bool scoped = (packet->connection_id != 0U || packet->session_epoch != 0U);
    if (scoped) {
        return transport_get_or_create_peer_scope(ctx, packet->source_id,
                                                  packet->connection_id,
                                                  packet->session_epoch);
    }

    return transport_get_or_create_peer(ctx, packet->source_id);
}

xgl_error_t transport_prepare_rx_peer(xgl_transport_ctx_t *ctx,
                                      const xgl_packet_t *packet,
                                      xgl_transport_peer_state_t **peer)
{
    *peer = NULL;

    if (packet->session_id == 0U) {
        return XGL_OK;
    }

    *peer = transport_find_rx_peer(ctx, packet);
    if (*peer == NULL) {
        *peer = transport_get_or_create_rx_peer(ctx, packet);
        if (*peer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
        transport_reset_peer_state(ctx, *peer, packet->session_id,
                                   packet->connection_id,
                                   packet->session_epoch);
        return XGL_OK;
    }

    if (packet->session_id != (*peer)->session_id) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    return XGL_OK;
}
