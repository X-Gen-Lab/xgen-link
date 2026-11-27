/**
 * \file            xgl_transport_peer.c
 * \brief           Exact peer identity, lifecycle and bounded reclamation
 */

#include "xgl_transport_internal.h"
#include "xgl_transport_rx_internal.h"
#include <internal/xgl_diagnostics.h>
#include <string.h>

/**
 * \brief           Check for buffered DATA that prevents peer reclamation
 * \param[in]       peer: Peer state
 * \return          true if the transport still owns buffered DATA
 */
static bool
transport_peer_has_buffered_data(const xgl_transport_peer_state_t* peer) {
#if XGL_FEATURE_OUT_OF_ORDER
    return peer->rx_buffered != NULL;
#else
    (void)peer;
    return false;
#endif
}

bool transport_peer_can_reclaim(const xgl_transport_peer_state_t* peer) {
    return !peer->failed && peer->tx_window.next_packet_number == 0U &&
           !peer->rx_has_packet_number_state &&
           xgl_reliable_is_empty(&peer->reliable_queue) &&
           !transport_peer_has_buffered_data(peer);
}

bool transport_peer_reclaim_timeout(const xgl_transport_ctx_t* ctx,
                                    const xgl_transport_peer_state_t* peer,
                                    uint32_t now_ms, uint32_t* timeout_ms) {
    if (!transport_peer_can_reclaim(peer)) {
        return false;
    }

    bool active = false;
    if (peer->hello_reserved) {
        uint32_t elapsed = now_ms - peer->hello_reserved_ms;
        *timeout_ms = elapsed >= XGL_TRANSPORT_HELLO_LEASE_MS
                          ? 0U
                          : XGL_TRANSPORT_HELLO_LEASE_MS - elapsed;
        active = true;
    }
    if (ctx->peer_idle_timeout_ms != 0U) {
        uint32_t elapsed = now_ms - peer->last_active_ms;
        uint32_t remaining = elapsed >= ctx->peer_idle_timeout_ms
                                 ? 0U
                                 : ctx->peer_idle_timeout_ms - elapsed;
        if (!active || remaining < *timeout_ms) {
            *timeout_ms = remaining;
        }
        active = true;
    }
    return active;
}

static void transport_clear_peer_data(xgl_transport_ctx_t* ctx,
                                      xgl_transport_peer_state_t* peer);

/**
 * \brief           Find an exact node, connection and epoch owner
 * \param[in]       ctx: Transport context; lookup does not change ownership
 * \param[in]       peer_id: Remote endpoint
 * \param[in]       connection_id: Connection identity, including zero
 * \param[in]       session_epoch: Session epoch, including zero
 * \return          Matching owner, or NULL
 */
xgl_transport_peer_state_t*
transport_find_peer_scope(const xgl_transport_ctx_t* ctx, uint16_t peer_id,
                          uint32_t connection_id, uint32_t session_epoch) {
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

    peer = (xgl_transport_peer_state_t*)xgl_alloc(
        ctx->memory.peer, sizeof(xgl_transport_peer_state_t));
    if (peer == NULL) {
        return NULL;
    }

    memset(peer, 0, sizeof(*peer));
    peer->peer_id = peer_id;
    peer->connection_id = connection_id;
    peer->session_epoch = session_epoch;
    peer->last_active_ms = transport_now(ctx);
#if XGL_FEATURE_ADAPTIVE_RTO
    xgl_rtt_init(&peer->rtt_est);
#endif

    xgl_error_t err = xgl_window_init(&peer->tx_window, ctx->window_size);
    if (err != XGL_OK) {
        xgl_free(ctx->memory.peer, peer);
        return NULL;
    }

    err = xgl_reliable_init(&peer->reliable_queue, ctx->memory.tx_packet);
    if (err != XGL_OK) {
        xgl_window_destroy(&peer->tx_window);
        xgl_free(ctx->memory.peer, peer);
        return NULL;
    }

    peer->reliable_queue.data_allocator = ctx->memory.tx_payload;
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
        xgl_free(ctx->memory.peer, peer);
        peer = next;
    }
    ctx->peers = NULL;
#if XGL_FEATURE_ADVANCED_RUNTIME
    ctx->maintenance_cursor = NULL;
#endif
}

/**
 * \brief           Reclaim peer states that have been idle beyond the
 *                  configured timeout, or whose unused HELLO lease expired
 * \param[in,out]   ctx: Transport context
 * \param[in]       current_time_ms: Explicit current time
 * \return          Number of peers reclaimed.
 */
uint32_t transport_reclaim_idle_peers(xgl_transport_ctx_t* ctx,
                                      uint32_t current_time_ms) {
    if (ctx == NULL) {
        return 0U;
    }

    uint32_t reclaimed = 0U;
    xgl_transport_peer_state_t** prev = &ctx->peers;
    xgl_transport_peer_state_t* peer = ctx->peers;

    while (peer != NULL) {
        xgl_transport_peer_state_t* next = peer->next;

        /* Accepted DATA history survives idle time so the same scope cannot
         * restart its packet numbers or lose duplicate suppression. */

        uint32_t remaining = 0U;
        if (transport_peer_reclaim_timeout(ctx, peer, current_time_ms,
                                           &remaining) &&
            remaining == 0U) {
            *prev = next;
#if XGL_FEATURE_ADVANCED_RUNTIME
            if (ctx->maintenance_cursor == peer) {
                ctx->maintenance_cursor = next;
            }
#endif
            xgl_reliable_destroy(&peer->reliable_queue);
            xgl_window_destroy(&peer->tx_window);
            xgl_free(ctx->memory.peer, peer);
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
#if XGL_FEATURE_ADVANCED_RUNTIME
    peer->tx_busy = false;
    peer->work_blocked = false;
#endif
#if XGL_FEATURE_OUT_OF_ORDER
    transport_clear_rx_buffered(ctx, peer);
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
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (ctx->scope_event_callback != NULL) {
        ctx->scope_event_callback(handle, peer->peer_id, peer->connection_id,
                                  peer->session_epoch, XGL_SCOPE_FAILED, error,
                                  ctx->extended_user_data);
    }
#endif

    transport_count_send_error(ctx);
    xgl_transport_report_error(
        ctx, handle, error,
        XGL_ERROR_MESSAGE("Reliable peer failed; new epoch required"));
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
                              transport_peer_has_buffered_data(peer));
#if XGL_FEATURE_ADVANCED_RUNTIME
            const bool notify =
                !peer->failed && (cancelled || peer->hello_reserved ||
                                  peer->rx_has_packet_number_state ||
                                  peer->tx_window.next_packet_number != 0U);
            if (ctx->maintenance_cursor == peer) {
                ctx->maintenance_cursor = peer->next;
            }
#endif
            *link = peer->next;
            transport_clear_peer_data(ctx, peer);
            xgl_window_destroy(&peer->tx_window);
            xgl_free(ctx->memory.peer, peer);
#if XGL_FEATURE_ADVANCED_RUNTIME
            if (notify && ctx->scope_event_callback != NULL) {
                ctx->scope_event_callback(handle, remote_id, connection_id,
                                          session_epoch, XGL_SCOPE_FAILED,
                                          XGL_ERR_CANCELLED,
                                          ctx->extended_user_data);
            }
#endif
            if (cancelled) {
                xgl_transport_report_error(
                    ctx, handle, XGL_ERR_CANCELLED,
                    XGL_ERROR_MESSAGE(
                        "Transport scope closed with pending data"));
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

#if XGL_FEATURE_ADVANCED_RUNTIME
void transport_note_tx_admission(xgl_transport_ctx_t* ctx,
                                 xgl_transport_peer_state_t* peer) {
    if (!peer->tx_busy && !peer->failed) {
        peer->tx_busy = true;
        peer->tx_started_ms = transport_now(ctx);
    }
}

void transport_notify_tx_drained(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                 xgl_transport_peer_state_t* peer) {
    if (!peer->tx_busy || !xgl_reliable_is_empty(&peer->reliable_queue)) {
        return;
    }
    peer->tx_busy = false;
    if (ctx->scope_event_callback != NULL) {
        ctx->scope_event_callback(handle, peer->peer_id, peer->connection_id,
                                  peer->session_epoch, XGL_SCOPE_TX_DRAINED,
                                  XGL_OK, ctx->extended_user_data);
    }
}

bool transport_expire_tx(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                         xgl_transport_peer_state_t* peer) {
    if (peer->tx_busy && ctx->tx_lifetime_ms != 0U &&
        transport_now(ctx) - peer->tx_started_ms >= ctx->tx_lifetime_ms) {
        transport_fail_peer(ctx, handle, peer, XGL_ERR_TIMEOUT);
        return true;
    }
    return false;
}

xgl_error_t xgl_transport_get_resources(const xgl_transport_ctx_t* ctx,
                                        xgl_resource_snapshot_t* resources) {
    if (ctx == NULL || resources == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(resources, 0, sizeof(*resources));
    for (const xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        resources->peer_count++;
        resources->failed_peers += peer->failed ? 1U : 0U;
        xgl_list_node_t* node;
        XGL_LIST_FOR_EACH(&peer->reliable_queue.wait_ack_list, node) {
            /* The reliable queue owns this intrusive record. */
            const xgl_reliable_packet_t* packet =
                /* NOLINTNEXTLINE(bugprone-casting-through-void) */
                XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
            resources->tx_packets++;
            resources->tx_payload_bytes += packet->data_len;
        }
#if XGL_FEATURE_OUT_OF_ORDER
        for (const xgl_transport_rx_buffered_packet_t* packet =
                 peer->rx_buffered;
             packet != NULL; packet = packet->next) {
            resources->rx_buffered_packets++;
            resources->rx_buffered_bytes += packet->packet_data.data_len;
        }
#endif
    }
    return XGL_OK;
}
#endif
