/**
 * \file            xgl_transport_runtime.c
 * \brief           Transport runtime processing and utility APIs
 */

#include "xgl_transport_internal.h"

/**
 * \brief           Record a transport send failure
 * \param[in,out]   ctx: Transport layer context
 */
void transport_count_send_error(xgl_transport_ctx_t* ctx) {
    if (ctx != NULL && ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

/**
 * \brief           Periodic transport layer processing
 */
xgl_error_t xgl_transport_run(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                              uint32_t current_time_ms) {
    if (!ctx) {
        return XGL_ERR_NULL_POINTER;
    }

    ctx->current_time_ms = current_time_ms;
#if XGL_FEATURE_FRAGMENTATION || XGL_FEATURE_OUT_OF_ORDER
    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        if (peer->failed) {
            continue;
        }
#if XGL_FEATURE_FRAGMENTATION
        (void)transport_pump_tx_message(ctx, handle, peer);
        if (peer->failed) {
            continue;
        }
        if (transport_drain_pending_message(ctx, handle, peer) != XGL_OK) {
            continue;
        }
#endif
#if XGL_FEATURE_OUT_OF_ORDER
        (void)transport_drain_rx_buffered(ctx, handle, peer);
#endif
    }
#endif

    uint32_t retransmit_count =
        transport_process_retransmissions(ctx, handle, current_time_ms);

    /* Update retransmission statistics */
    if (retransmit_count > 0 && ctx->tx_retries != NULL) {
        (*ctx->tx_retries) += retransmit_count;
    }

#if XGL_FEATURE_FRAGMENTATION
    /* A timed-out reliable reassembly contains acknowledged bytes. Failure
     * must be explicit before its storage can be released. */
    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        if (peer->failed || !peer->rx_has_packet_number_state ||
            ctx->fragment_mgr == NULL) {
            continue;
        }
        xgct_list_node_t* node;
        XGCT_LIST_FOR_EACH(&ctx->fragment_mgr->reassembly_list, node) {
            const xgl_reassembly_buffer_t* buffer =
                /* Intrusive node membership is established by the owning list.
                 */
                /* NOLINTNEXTLINE(bugprone-casting-through-void) */
                XGCT_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
            if (buffer->source_id == peer->peer_id &&
                buffer->connection_id == peer->connection_id &&
                buffer->session_epoch == peer->session_epoch &&
                buffer->received_bytes > 0U && buffer->timeout_ms != 0U &&
                current_time_ms - buffer->first_fragment_time >=
                    buffer->timeout_ms) {
                transport_fail_peer(ctx, handle, peer, XGL_ERR_TIMEOUT);
                ctx->stats->rx_dropped++;
                break;
            }
        }
    }

    /* Process fragment reassembly timeouts */
    if (ctx->fragment_mgr) {
        uint32_t timeout_count =
            xgl_fragment_process_timeouts(ctx->fragment_mgr, current_time_ms);
        if (timeout_count > 0) {
            /* Report error */
            if (ctx->error_callback) {
                ctx->error_callback(handle, XGL_ERR_TIMEOUT,
                                    "Fragment reassembly timeout",
                                    ctx->callback_user_data);
            }

            /* Update statistics */
            ctx->stats->rx_dropped += timeout_count;
        }
    }

#endif

    /* Reclaim idle peer states */
    (void)transport_reclaim_idle_peers(ctx, current_time_ms);

    return XGL_OK;
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

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Merge a timer into the earliest remaining delay
 * \param[in]       now_ms: Current time
 * \param[in]       start_ms: Timer start
 * \param[in]       interval_ms: Timer duration
 * \param[in,out]   active: Whether any timer has been collected
 * \param[in,out]   timeout_ms: Earliest remaining delay
 */
static void transport_take_timeout(uint32_t now_ms, uint32_t start_ms,
                                   uint32_t interval_ms, bool* active,
                                   uint32_t* timeout_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    uint32_t elapsed = now_ms - start_ms;
    uint32_t remaining = elapsed >= interval_ms ? 0U : interval_ms - elapsed;
    if (!*active || remaining < *timeout_ms) {
        *timeout_ms = remaining;
    }
    *active = true;
}

/**
 * \brief           Collect reliable, peer-idle and reassembly deadlines
 * \param[in]       ctx: Transport layer context
 * \param[in]       now_ms: Current time
 * \param[out]      timeout_ms: Remaining delay; zero means due now
 * \return          true if a timer is active
 */
bool xgl_transport_next_timeout(const xgl_transport_ctx_t* ctx, uint32_t now_ms,
                                uint32_t* timeout_ms) {
    if (ctx == NULL || timeout_ms == NULL) {
        return false;
    }
    bool active = false;
    *timeout_ms = 0U;
    for (const xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        xgct_list_node_t* node;
        XGCT_LIST_FOR_EACH(&peer->reliable_queue.wait_ack_list, node) {
            const xgl_reliable_packet_t* packet =
                /* Intrusive node membership is established by the owning list.
                 */
                /* NOLINTNEXTLINE(bugprone-casting-through-void) */
                XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
            if (packet->retry_pending) {
                transport_take_timeout(now_ms, packet->retry_started_ms,
                                       transport_retry_delay_ms(ctx), &active,
                                       timeout_ms);
            } else if (packet->sent && packet->timeout_ms > 0) {
                transport_take_timeout(now_ms, packet->send_timestamp,
                                       (uint32_t)packet->timeout_ms, &active,
                                       timeout_ms);
            }
        }
#if XGL_FEATURE_FRAGMENTATION
        if (peer->tx_message.data != NULL && peer->tx_message_retry_pending) {
            transport_take_timeout(now_ms, peer->tx_message_retry_started_ms,
                                   transport_retry_delay_ms(ctx), &active,
                                   timeout_ms);
        }
#endif
        if (ctx->peer_idle_timeout_ms != 0U && !peer->failed &&
            peer->tx_window.next_packet_number == 0U &&
            !peer->rx_has_packet_number_state &&
            xgl_reliable_is_empty(&peer->reliable_queue) &&
            !transport_peer_has_pending_data(ctx, peer)) {
            transport_take_timeout(now_ms, peer->last_active_ms,
                                   ctx->peer_idle_timeout_ms, &active,
                                   timeout_ms);
        }
    }
#if XGL_FEATURE_FRAGMENTATION
    if (ctx->fragment_mgr != NULL) {
        xgct_list_node_t* node;
        XGCT_LIST_FOR_EACH(&ctx->fragment_mgr->reassembly_list, node) {
            const xgl_reassembly_buffer_t* buffer =
                /* Intrusive node membership is established by the owning list.
                 */
                /* NOLINTNEXTLINE(bugprone-casting-through-void) */
                XGCT_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
            if (buffer->received_bytes > 0U && buffer->timeout_ms != 0U) {
                transport_take_timeout(now_ms, buffer->first_fragment_time,
                                       buffer->timeout_ms, &active, timeout_ms);
            }
        }
    }
#endif
    return active;
}

/**
 * \brief           Report error through error callback
 */
void xgl_transport_report_error(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                xgl_error_t error, const char* message) {
    if (!ctx) {
        return;
    }

    if (ctx->error_callback) {
        ctx->error_callback(handle, error, message, ctx->callback_user_data);
    }
}
