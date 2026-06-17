/**
 * \file            xgl_transport_rx_order.c
 * \brief           Transport receive ordering and delivery
 */

#include "xgl_transport_internal.h"

xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t *ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t *peer)
{
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    for (;;) {
        xgl_transport_rx_buffered_packet_t *buffered =
            transport_take_rx_buffered(peer, peer->rx_next_packet_number);
        if (buffered == NULL) {
            return XGL_OK;
        }

        uint32_t packet_number = buffered->packet.packet_number;
        xgl_error_t err = transport_deliver_packet(
            ctx, handle, &buffered->packet, buffered->data, buffered->data_len);
        (void) transport_send_ack(
            ctx, handle, packet_number, buffered->packet.source_id,
            buffered->packet.session_id, buffered->packet.connection_id,
            buffered->packet.session_epoch);
        transport_free_rx_buffered_packet(ctx, buffered);
        if (err != XGL_OK) {
            return err;
        }
        peer->rx_next_packet_number = packet_number + 1U;
    }
}
