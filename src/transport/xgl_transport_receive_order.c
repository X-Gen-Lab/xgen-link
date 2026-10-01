/**
 * \file            xgl_transport_receive_order.c
 * \brief           Reliable receive ordering decisions
 */

#include "xgl_transport_internal.h"

/**
 * \brief           Classify duplicates and retain supported out-of-order
 *                  packets
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Borrowed reliable packet view
 * \param[in,out]   peer: Receive peer, created on first reliable packet
 * \return          XGL_OK or a receive admission error
 */
xgl_error_t transport_process_reliable_rx_order(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle, const xgl_packet_t* packet,
    xgl_transport_peer_state_t** peer) {
    if (*peer == NULL) {
        *peer = transport_get_or_create_rx_peer(ctx, packet);
        if (*peer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
    }

    uint32_t packet_number = transport_receive_packet_number(packet);
    if (!(*peer)->rx_has_packet_number_state) {
        (*peer)->rx_next_packet_number = 0U;
        (*peer)->rx_has_packet_number_state = true;
    }

    if (packet_number < (*peer)->rx_next_packet_number) {
        (void)transport_send_ack(ctx, handle, packet_number, packet->source_id,
                                 packet->connection_id, packet->session_epoch);
        return XGL_OK;
    }

    if (packet_number > (*peer)->rx_next_packet_number) {
#if XGL_FEATURE_OUT_OF_ORDER
        uint32_t expected_packet_number = (*peer)->rx_next_packet_number;
        xgl_error_t err = transport_cache_out_of_order_packet(
            ctx, *peer, packet, packet_number);
        (void)transport_send_sack(ctx, handle, *peer, packet->source_id,
                                  expected_packet_number, packet->connection_id,
                                  packet->session_epoch);
        if (err != XGL_OK && ctx->stats != NULL) {
            ctx->stats->rx_dropped++;
        }
        return err;
#else
        return XGL_ERR_SEQUENCE_ERROR;
#endif
    }

    /* The in-order packet is acknowledged only after delivery/admission. */
    return XGL_OK;
}
