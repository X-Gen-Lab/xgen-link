/**
 * \file            xgl_transport_runtime.c
 * \brief           Bounded retransmission, maintenance and deadlines
 */

#include "xgl_transport_internal.h"
#include <internal/xgl_diagnostics.h>

/**
 * \brief           Retry an owned packet or fail its peer at the retry limit
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Owning peer
 * \param[in,out]   rel_packet: Packet retained until ACK or peer failure
 * \param[in]       current_time_ms: Current transport clock
 * \return          Send result or XGL_ERR_ACK_TIMEOUT at the retry limit
 */
xgl_error_t transport_retransmit_reliable_packet(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle,
    xgl_transport_peer_state_t* peer, xgl_reliable_packet_t* rel_packet,
    uint32_t current_time_ms) {
    if (ctx == NULL || peer == NULL || rel_packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        transport_fail_peer(ctx, handle, peer, XGL_ERR_INVALID_PARAM);
        return XGL_ERR_INVALID_PARAM;
    }
    if (peer->failed) {
        return XGL_ERR_ACK_TIMEOUT;
    }
    if (rel_packet->retry_count >= ctx->max_retry_count) {
        transport_fail_peer(ctx, handle, peer, XGL_ERR_ACK_TIMEOUT);
        return XGL_ERR_ACK_TIMEOUT;
    }

    if (rel_packet->retry_pending &&
        current_time_ms - rel_packet->retry_started_ms <
            transport_retry_delay_ms(ctx)) {
        return XGL_ERR_BUSY;
    }

    xgl_packet_data_t packet_data = {.data_len = rel_packet->data_len,
                                     .data = rel_packet->data};

    xgl_packet_t packet = {.source_id = rel_packet->source_id,
                           .target_id = rel_packet->target_id,
                           .connection_id = rel_packet->connection_id,
                           .packet_number = rel_packet->packet_number,
                           .session_epoch = rel_packet->session_epoch,
                           .packet_type = rel_packet->packet_type,
                           .flags = rel_packet->flags,
                           .data_type = rel_packet->data_type,
                           .reliable = XGL_RELIABILITY_ACK_ELICITING,
                           .fragment = rel_packet->fragment,
                           .priority = rel_packet->priority,
                           .data = &packet_data,
                           .extensions = rel_packet->extensions,
                           .extensions_len = rel_packet->extensions_len,
                           .phy = NULL};

    xgl_error_t err = xgl_packet_send(ctx->lower_layer, handle, &packet);
    if (err == XGL_ERR_BUSY || err == XGL_ERR_NO_MEMORY) {
        rel_packet->retry_pending = true;
        rel_packet->retry_started_ms = current_time_ms;
        return err;
    }
    if (err != XGL_OK) {
        transport_fail_peer(ctx, handle, peer, err);
        return err;
    }

    rel_packet->retry_pending = false;
    rel_packet->retry_count++;
    rel_packet->timeout_ms = xgl_reliable_calc_backoff(
        rel_packet->initial_timeout_ms, rel_packet->retry_count);
    rel_packet->send_timestamp = current_time_ms;
    rel_packet->sent = true;

    return XGL_OK;
}

/**
 * \brief           Retry due packets of one peer
 * \param[in,out]   ctx: Transport layer context
 * \param[in,out]   peer: Owning peer
 * \param[in]       handle: Protocol instance handle
 * \param[in]       current_time_ms: Current transport clock
 * \return          Number of successful retransmissions
 */
static uint32_t transport_process_retransmission_queue(
    xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    xgl_handle_t handle, uint32_t current_time_ms) {
    uint32_t retransmit_count = 0;
    xgl_reliable_queue_t* queue = &peer->reliable_queue;
    xgct_list_node_t* node;
    xgct_list_node_t* tmp;

    XGCT_LIST_FOR_EACH_SAFE(&queue->wait_ack_list, node, tmp) {
        xgl_reliable_packet_t* rel_packet =
            /* Intrusive node membership is established by the owning list. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);

        if (!rel_packet->sent) {
            continue;
        }

        uint32_t elapsed_ms = current_time_ms - rel_packet->send_timestamp;
        if (!rel_packet->retry_pending &&
            elapsed_ms < (uint32_t)rel_packet->timeout_ms) {
            continue;
        }

        if (transport_retransmit_reliable_packet(ctx, handle, peer, rel_packet,
                                                 current_time_ms) == XGL_OK) {
            retransmit_count++;
        }
        /* Failure clears the entire queue. */
        if (peer->failed) {
            break;
        }
    }

    return retransmit_count;
}

/**
 * \brief           Process each peer-owned retransmission queue
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       current_time_ms: Current transport clock
 * \return          Number of successful retransmissions
 */
static uint32_t transport_process_retransmissions(xgl_transport_ctx_t* ctx,
                                                  xgl_handle_t handle,
                                                  uint32_t current_time_ms) {
    uint32_t retransmit_count = 0;

    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        retransmit_count += transport_process_retransmission_queue(
            ctx, peer, handle, current_time_ms);
    }

    return retransmit_count;
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
    if (XGL_FEATURE_STATISTICS && retransmit_count > 0 &&
        ctx->tx_retries != NULL) {
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
                if (XGL_FEATURE_STATISTICS && ctx->stats != NULL) {
                    ctx->stats->rx_dropped++;
                }
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
                ctx->error_callback(
                    handle, XGL_ERR_TIMEOUT,
                    XGL_ERROR_MESSAGE("Fragment reassembly timeout"),
                    ctx->callback_user_data);
            }

            /* Update statistics */
            if (XGL_FEATURE_STATISTICS && ctx->stats != NULL) {
                ctx->stats->rx_dropped += timeout_count;
            }
        }
    }

#endif

    /* Reclaim idle peer states */
    (void)transport_reclaim_idle_peers(ctx, current_time_ms);

    return XGL_OK;
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
        if (ctx->peer_idle_timeout_ms != 0U &&
            transport_peer_can_reclaim(ctx, peer)) {
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
