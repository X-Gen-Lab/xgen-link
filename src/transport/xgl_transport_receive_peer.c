/**
 * \file            xgl_transport_receive_peer.c
 * \brief           Exact transport peer selection
 */

#include "xgl_transport_internal.h"

/**
 * \brief           Find the exact peer identified by received metadata
 * \param[in,out]   ctx: Transport context
 * \param[in]       packet: Received packet metadata
 * \return          Matching peer, or NULL
 */
xgl_transport_peer_state_t* transport_find_rx_peer(xgl_transport_ctx_t* ctx,
                                                   const xgl_packet_t* packet) {
    return transport_find_peer_scope(
        ctx, packet->source_id, packet->connection_id, packet->session_epoch);
}

/**
 * \brief           Admit the exact peer identified by received metadata
 * \param[in,out]   ctx: Transport context
 * \param[in]       packet: Received packet metadata
 * \return          Matching peer, or NULL when capacity is unavailable
 */
xgl_transport_peer_state_t*
transport_get_or_create_rx_peer(xgl_transport_ctx_t* ctx,
                                const xgl_packet_t* packet) {
    return transport_get_or_create_peer_scope(
        ctx, packet->source_id, packet->connection_id, packet->session_epoch);
}

/**
 * \brief           Resolve receive ownership without creating another identity
 * \param[in,out]   ctx: Transport context
 * \param[in]       packet: Received packet metadata
 * \param[out]      peer: Existing matching peer, possibly NULL
 * \return          XGL_OK or failure of an existing peer
 */
xgl_error_t transport_prepare_rx_peer(xgl_transport_ctx_t* ctx,
                                      const xgl_packet_t* packet,
                                      xgl_transport_peer_state_t** peer) {
    *peer = transport_find_rx_peer(ctx, packet);
    if (*peer != NULL && (*peer)->failed) {
        return XGL_ERR_ACK_TIMEOUT;
    }
    return XGL_OK;
}
