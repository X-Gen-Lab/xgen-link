/**
 * \file            xgl_transport_peer.c
 * \brief           Exact peer identity, lifecycle and bounded reclamation
 */

#include "xgl_transport_internal.h"
#include <string.h>

/**
 * \brief           Check for owned message data that prevents peer reclamation
 * \param[in]       ctx: Transport layer context
 * \param[in]       peer: Peer state
 * \return          true if message data is still owned by the transport
 */
static bool
transport_peer_has_pending_data(const xgl_transport_ctx_t* ctx,
                                const xgl_transport_peer_state_t* peer) {
#if XGL_FEATURE_OUT_OF_ORDER
    if (peer->rx_buffered != NULL) {
        return true;
    }
#endif
#if XGL_FEATURE_FRAGMENTATION
    if (peer->rx_pending_message.data != NULL ||
        peer->tx_message.data != NULL) {
        return true;
    }
    if (ctx->fragment_mgr != NULL) {
        xgct_list_node_t* node;
        XGCT_LIST_FOR_EACH(&ctx->fragment_mgr->reassembly_list, node) {
            const xgl_reassembly_buffer_t* buffer =
                /* Intrusive node membership is established by the owning list.
                 */
                /* NOLINTNEXTLINE(bugprone-casting-through-void) */
                XGCT_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
            if (buffer->source_id == peer->peer_id &&
                buffer->connection_id == peer->connection_id &&
                buffer->session_epoch == peer->session_epoch) {
                return true;
            }
        }
    }
#endif
    (void)ctx;
    (void)peer;
    return false;
}

bool transport_peer_can_reclaim(const xgl_transport_ctx_t* ctx,
                                const xgl_transport_peer_state_t* peer) {
    return !peer->failed && peer->tx_window.next_packet_number == 0U &&
           !peer->rx_has_packet_number_state &&
           xgl_reliable_is_empty(&peer->reliable_queue) &&
           !transport_peer_has_pending_data(ctx, peer);
}

static void transport_clear_peer_data(xgl_transport_ctx_t* ctx,
                                      xgl_transport_peer_state_t* peer);

/**
 * \brief           Find an exact node, connection and epoch owner
 * \param[in,out]   ctx: Transport context
 * \param[in]       peer_id: Remote endpoint
 * \param[in]       connection_id: Connection identity, including zero
 * \param[in]       session_epoch: Session epoch, including zero
 * \return          Matching owner, or NULL
 */
xgl_transport_peer_state_t* transport_find_peer_scope(xgl_transport_ctx_t* ctx,
                                                      uint16_t peer_id,
                                                      uint32_t connection_id,
                                                      uint32_t session_epoch) {
    if (ctx == NULL) {
        return NULL;
    }

    xgl_transport_peer_state_t* peer = ctx->peers;
    while (peer != NULL) {
        if (peer->peer_id == peer_id && peer->connection_id == connection_id &&
            peer->session_epoch == session_epoch) {
            return peer;
        }
        peer = peer->next;
    }

    return NULL;
}

/**
 * \brief           Find a peer or allocate one within the configured peer limit
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       peer_id: Remote node ID
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Session identity
 * \return          Owned peer state, or NULL when capacity is unavailable
 */
xgl_transport_peer_state_t*
transport_get_or_create_peer_scope(xgl_transport_ctx_t* ctx, uint16_t peer_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch) {
    xgl_transport_peer_state_t* peer =
        transport_find_peer_scope(ctx, peer_id, connection_id, session_epoch);
    if (peer != NULL) {
        return peer;
    }

    size_t count = 0U;
    for (peer = ctx->peers; peer != NULL; peer = peer->next) {
        count++;
    }
    if (count >= ctx->max_peers) {
        return NULL;
    }

    peer = (xgl_transport_peer_state_t*)xgm_alloc(
        ctx->memory.peer, sizeof(xgl_transport_peer_state_t));
    if (peer == NULL) {
        return NULL;
    }

    memset(peer, 0, sizeof(*peer));
    peer->peer_id = peer_id;
    peer->connection_id = connection_id;
    peer->session_epoch = session_epoch;
    peer->last_active_ms = transport_now(ctx);
    xgl_rtt_init(&peer->rtt_est);

    xgl_error_t err = xgl_window_init_with_allocator(
        &peer->tx_window, ctx->window_size, ctx->memory.window);
    if (err != XGL_OK) {
        xgm_free(ctx->memory.peer, peer);
        return NULL;
    }

    err = xgl_reliable_init(&peer->reliable_queue, ctx->memory.tx_packet);
    if (err != XGL_OK) {
        xgl_window_destroy(&peer->tx_window);
        xgm_free(ctx->memory.peer, peer);
        return NULL;
    }

    peer->reliable_queue.data_allocator = ctx->memory.tx_payload;
#if XGL_FEATURE_FRAGMENTATION
    peer->reliable_queue.extensions_allocator = ctx->memory.tx_extensions;
#endif
    peer->next = ctx->peers;
    ctx->peers = peer;
    return peer;
}

void transport_destroy_peers(xgl_transport_ctx_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    xgl_transport_peer_state_t* peer = ctx->peers;
    while (peer != NULL) {
        xgl_transport_peer_state_t* next = peer->next;
        transport_clear_peer_data(ctx, peer);
        xgl_window_destroy(&peer->tx_window);
        xgm_free(ctx->memory.peer, peer);
        peer = next;
    }
    ctx->peers = NULL;
}

/**
 * \brief           Reclaim peer states that have been idle beyond the
 *                  configured timeout without ever using reliable DATA numbers.
 * \param[in,out]   ctx: Transport context
 * \param[in]       current_time_ms: Explicit current time
 * \return          Number of peers reclaimed.
 */
uint32_t transport_reclaim_idle_peers(xgl_transport_ctx_t* ctx,
                                      uint32_t current_time_ms) {
    if (ctx == NULL || ctx->peer_idle_timeout_ms == 0U) {
        return 0U;
    }

    uint32_t reclaimed = 0U;
    xgl_transport_peer_state_t** prev = &ctx->peers;
    xgl_transport_peer_state_t* peer = ctx->peers;

    while (peer != NULL) {
        xgl_transport_peer_state_t* next = peer->next;

        /* Accepted DATA history survives idle time so the same scope cannot
         * restart its packet numbers or lose duplicate suppression. */

        uint32_t idle_ms = current_time_ms - peer->last_active_ms;
        if (transport_peer_can_reclaim(ctx, peer) &&
            idle_ms >= ctx->peer_idle_timeout_ms) {
            *prev = next;
            xgl_reliable_destroy(&peer->reliable_queue);
            xgl_window_destroy(&peer->tx_window);
            xgm_free(ctx->memory.peer, peer);
            reclaimed++;
        } else {
            prev = &peer->next;
        }

        peer = next;
    }

    return reclaimed;
}

/**
 * \brief           Release all data owned by one scope without reusing its
 * numbers
 * \param[in,out]   ctx: Transport context
 * \param[in,out]   peer: Exact owner whose identity and failure state are
 * retained
 */
static void transport_clear_peer_data(xgl_transport_ctx_t* ctx,
                                      xgl_transport_peer_state_t* peer) {
    xgl_reliable_clear(&peer->reliable_queue);
#if XGL_FEATURE_OUT_OF_ORDER
    transport_clear_rx_buffered(ctx, peer);
#endif
#if XGL_FEATURE_FRAGMENTATION
    transport_clear_pending_message(ctx, peer);
    transport_clear_tx_message(ctx, peer);
    if (ctx->fragment_mgr != NULL) {
        (void)xgl_fragment_clear_reassembly_scope(
            ctx->fragment_mgr, peer->peer_id, peer->connection_id,
            peer->session_epoch);
    }
#else
    (void)ctx;
#endif
}

/**
 * \brief           Fail a peer and release all transport-owned outstanding data
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Peer that requires a new epoch before further traffic
 * \param[in]       error: Failure reported once to the application
 */
void transport_fail_peer(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                         xgl_transport_peer_state_t* peer, xgl_error_t error) {
    if (ctx == NULL || peer == NULL || peer->failed) {
        return;
    }
    peer->failed = true;
    transport_clear_peer_data(ctx, peer);

    transport_count_send_error(ctx);
    xgl_transport_report_error(ctx, handle, error,
                               "Reliable peer failed; new epoch required");
}

/**
 * \brief           Close one exact scope and return its peer storage
 * reservation
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle for cancellation reporting
 * \param[in]       remote_id: Remote endpoint
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Session epoch being retired
 * \return          XGL_OK, XGL_ERR_NOT_FOUND, or XGL_ERR_NULL_POINTER
 * \note            The caller closes authentication first, or drains
 * unauthenticated old traffic and changes epoch before reusing this
 * reservation.
 */
xgl_error_t xgl_transport_close_scope(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle, uint16_t remote_id,
                                      uint32_t connection_id,
                                      uint32_t session_epoch) {
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_transport_peer_state_t** link = &ctx->peers;
    while (*link != NULL) {
        xgl_transport_peer_state_t* peer = *link;
        if (peer->peer_id == remote_id &&
            peer->connection_id == connection_id &&
            peer->session_epoch == session_epoch) {
            bool cancelled = !peer->failed &&
                             (!xgl_reliable_is_empty(&peer->reliable_queue) ||
                              transport_peer_has_pending_data(ctx, peer));
            *link = peer->next;
            transport_clear_peer_data(ctx, peer);
            xgl_window_destroy(&peer->tx_window);
            xgm_free(ctx->memory.peer, peer);
            if (cancelled) {
                xgl_transport_report_error(
                    ctx, handle, XGL_ERR_CANCELLED,
                    "Transport scope closed with pending data");
            }
            return XGL_OK;
        }
        link = &peer->next;
    }
    return XGL_ERR_NOT_FOUND;
}

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
 * \brief           Query capacity for an exact peer and session scope
 * \param[in]       ctx: Transport layer context
 * \param[in]       peer_id: Remote node ID
 * \param[in]       connection_id: Connection scope ID
 * \param[in]       session_epoch: Session epoch
 * \return          true if a reliable packet can be accepted
 */
bool xgl_transport_can_send_to(const xgl_transport_ctx_t* ctx, uint16_t peer_id,
                               uint32_t connection_id, uint32_t session_epoch) {
    if (ctx == NULL) {
        return false;
    }
    if (transport_tx_packet_count(ctx) >= ctx->max_tx_packets) {
        return false;
    }
    size_t count = 0U;
    for (const xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        count++;
        if (peer->peer_id == peer_id && peer->connection_id == connection_id &&
            peer->session_epoch == session_epoch) {
            return transport_peer_can_send(peer);
        }
    }
    return count < ctx->max_peers;
}
