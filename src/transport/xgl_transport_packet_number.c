/**
 * \file            xgl_transport_packet_number.c
 * \brief           Transport packet number helpers
 */

#include "xgl_transport_internal.h"

static void
transport_advance_packet_window_if_possible(xgl_transport_ctx_t *ctx)
{
    if (ctx != NULL && xgl_window_can_send_packet_number(&ctx->window)) {
        xgl_window_advance_next_packet_number(&ctx->window);
    }
}

void transport_commit_packet_number(xgl_transport_ctx_t *ctx,
                                    xgl_transport_peer_state_t *peer)
{
    if (peer == NULL) {
        return;
    }

    xgl_window_advance_next_packet_number(&peer->tx_window);
    transport_advance_packet_window_if_possible(ctx);
}

uint32_t transport_receive_packet_number(const xgl_packet_t *packet)
{
    if (packet == NULL) {
        return 0U;
    }

    return packet->packet_number;
}
