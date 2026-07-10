/**
 * \file            xgl_transport_peer.c
 * \brief           Transport peer/session state helpers
 */

#include <string.h>

#include "xgl/internal/xgl_time.h"
#include "xgl_transport_internal.h"
xgl_transport_peer_state_t *transport_find_peer(xgl_transport_ctx_t *ctx,
                                                uint16_t peer_id)
{
    if (ctx == NULL) {
        return NULL;
    }

    xgl_transport_peer_state_t *peer = ctx->peers;
    while (peer != NULL) {
        if (peer->peer_id == peer_id) {
            return peer;
        }
        peer = peer->next;
    }

    return NULL;
}

xgl_transport_peer_state_t *transport_find_peer_scope(xgl_transport_ctx_t *ctx,
                                                      uint16_t peer_id,
                                                      uint32_t connection_id,
                                                      uint32_t session_epoch)
{
    if (ctx == NULL) {
        return NULL;
    }

    xgl_transport_peer_state_t *peer = ctx->peers;
    while (peer != NULL) {
        if (peer->peer_id == peer_id && peer->has_connection_scope &&
            peer->connection_id == connection_id &&
            peer->session_epoch == session_epoch) {
            return peer;
        }
        peer = peer->next;
    }

    return NULL;
}

static xgl_transport_peer_state_t *transport_get_or_create_peer_internal(
    xgl_transport_ctx_t *ctx, uint16_t peer_id, uint32_t connection_id,
    uint32_t session_epoch, bool has_connection_scope)
{
    xgl_transport_peer_state_t *peer =
        has_connection_scope ? transport_find_peer_scope(
                                   ctx, peer_id, connection_id, session_epoch)
                             : transport_find_peer(ctx, peer_id);
    if (peer != NULL) {
        return peer;
    }

    peer = (xgl_transport_peer_state_t *) transport_malloc(
        ctx->allocator, sizeof(xgl_transport_peer_state_t));
    if (peer == NULL) {
        return NULL;
    }

    memset(peer, 0, sizeof(*peer));
    peer->peer_id = peer_id;
    peer->has_connection_scope = has_connection_scope;
    peer->connection_id = has_connection_scope ? connection_id : 0U;
    peer->session_epoch = has_connection_scope ? session_epoch : 0U;
    peer->session_id = (uint16_t) (ctx->next_session_id & XGL_SESSION_ID_MASK);
    if (peer->session_id == 0U) {
        peer->session_id = 1U;
    }
    ctx->next_session_id =
        (uint16_t) ((peer->session_id + 1U) & XGL_SESSION_ID_MASK);
    if (ctx->next_session_id == 0U) {
        ctx->next_session_id = 1U;
    }
    xgl_rtt_init(&peer->rtt_est);

    xgl_error_t err = xgl_window_init_with_allocator(
        &peer->tx_window, ctx->window.window_size, ctx->allocator);
    if (err != XGL_OK) {
        transport_free(ctx->allocator, peer);
        return NULL;
    }

    err = xgl_reliable_init(&peer->reliable_queue, ctx->max_retry_count,
                            ctx->allocator);
    if (err != XGL_OK) {
        xgl_window_destroy(&peer->tx_window);
        transport_free(ctx->allocator, peer);
        return NULL;
    }

    peer->next = ctx->peers;
    ctx->peers = peer;
    return peer;
}

xgl_transport_peer_state_t *
transport_get_or_create_peer(xgl_transport_ctx_t *ctx, uint16_t peer_id)
{
    return transport_get_or_create_peer_internal(ctx, peer_id, 0U, 0U, false);
}

xgl_transport_peer_state_t *
transport_get_or_create_peer_scope(xgl_transport_ctx_t *ctx, uint16_t peer_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch)
{
    return transport_get_or_create_peer_internal(ctx, peer_id, connection_id,
                                                 session_epoch, true);
}

void transport_destroy_peers(xgl_transport_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    xgl_transport_peer_state_t *peer = ctx->peers;
    while (peer != NULL) {
        xgl_transport_peer_state_t *next = peer->next;
        transport_clear_rx_buffered(ctx, peer);
        xgl_reliable_destroy(&peer->reliable_queue);
        xgl_window_destroy(&peer->tx_window);
        transport_free(ctx->allocator, peer);
        peer = next;
    }
    ctx->peers = NULL;
}

/**
 * \brief           Reclaim peer states that have been idle beyond the configured
 *                  timeout and have no pending reliable or buffered RX state.
 * \return          Number of peers reclaimed.
 */
uint32_t transport_reclaim_idle_peers(xgl_transport_ctx_t *ctx,
                                      uint32_t current_time_ms)
{
    if (ctx == NULL || ctx->peer_idle_timeout_ms == 0U) {
        return 0U;
    }

    uint32_t reclaimed = 0U;
    xgl_transport_peer_state_t **prev = &ctx->peers;
    xgl_transport_peer_state_t *peer = ctx->peers;

    while (peer != NULL) {
        xgl_transport_peer_state_t *next = peer->next;

        /* Skip peers that still hold pending reliable packets or buffered
         * out-of-order RX data -- those must not be reclaimed.           */
        bool has_pending = !xgl_reliable_is_empty(&peer->reliable_queue) ||
                           peer->rx_buffered != NULL;

        uint32_t idle_ms = current_time_ms - peer->last_active_ms;
        if (!has_pending && idle_ms >= ctx->peer_idle_timeout_ms) {
            *prev = next;
            xgl_reliable_destroy(&peer->reliable_queue);
            xgl_window_destroy(&peer->tx_window);
            transport_free(ctx->allocator, peer);
            reclaimed++;
        } else {
            prev = &peer->next;
        }

        peer = next;
    }

    return reclaimed;
}

void transport_reset_peer_state(xgl_transport_ctx_t *ctx,
                                xgl_transport_peer_state_t *peer,
                                uint16_t session_id, uint32_t connection_id,
                                uint32_t session_epoch)
{
    if (ctx == NULL || peer == NULL) {
        return;
    }

    peer->session_id = (uint16_t) (session_id & XGL_SESSION_ID_MASK);
    peer->hello_sent = false;
    peer->session_established = true;
    xgl_reliable_clear(&peer->reliable_queue);
    xgl_window_reset(&peer->tx_window);
    xgl_window_reset(&ctx->window);
    if (ctx->fragment_mgr != NULL) {
        (void) xgl_fragment_clear_reassembly_scope(
            ctx->fragment_mgr, peer->peer_id, connection_id, session_epoch);
    }
    xgl_rtt_init(&peer->rtt_est);
    peer->rx_next_packet_number = 0U;
    peer->rx_has_packet_number_state = false;
    transport_clear_rx_buffered(ctx, peer);
    peer->last_active_ms = xgl_time_ms();
}

void transport_update_peer_deadline(xgl_transport_peer_state_t *peer)
{
    if (peer == NULL) {
        return;
    }

    peer->earliest_deadline_ms = 0U;

    xgl_list_node_t *node;
    XGL_LIST_FOR_EACH(&peer->reliable_queue.wait_ack_list, node) {
        const xgl_reliable_packet_t *pkt =
            XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        if (pkt->send_timestamp != 0U && pkt->timeout_ms > 0) {
            uint32_t abs_deadline = pkt->send_timestamp + (uint32_t)pkt->timeout_ms;
            if (peer->earliest_deadline_ms == 0U ||
                (int32_t)(abs_deadline - peer->earliest_deadline_ms) < 0) {
                peer->earliest_deadline_ms = abs_deadline;
            }
        }
    }
}
