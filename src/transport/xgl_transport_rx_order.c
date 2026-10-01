/**
 * \file            xgl_transport_rx_order.c
 * \brief           Transport receive ordering and delivery
 */

#include "xgl_transport_internal.h"

/**
 * \brief           Deliver contiguous owned packets without discarding busy
 *                  data
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Owning receive peer
 * \return          XGL_OK after draining, or the first admission error
 */
xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t* ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t* peer) {
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    for (;;) {
        xgl_transport_rx_buffered_packet_t* buffered = peer->rx_buffered;
        if (buffered == NULL ||
            buffered->packet.packet_number != peer->rx_next_packet_number) {
            return XGL_OK;
        }

        uint32_t packet_number = buffered->packet.packet_number;
        xgl_error_t err = transport_deliver_packet(
            ctx, handle, &buffered->packet, buffered->data, buffered->data_len);
        if (err != XGL_OK) {
            /* Already advertised through SACK: retain it without depending on
             * another sender retransmission when the application is busy. */
            if (err != XGL_ERR_BUSY && err != XGL_ERR_NO_MEMORY) {
                transport_fail_peer(ctx, handle, peer, err);
            }
            return err;
        }
        (void)transport_send_ack(
            ctx, handle, packet_number, buffered->packet.source_id,
            buffered->packet.connection_id, buffered->packet.session_epoch);
        (void)transport_take_rx_buffered(peer, packet_number);
        transport_free_rx_buffered_packet(ctx, buffered);
        peer->rx_next_packet_number = packet_number + 1U;
    }
}
