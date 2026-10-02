/**
 * \file            xgl_transport_runtime.c
 * \brief           Bounded retransmission, maintenance and deadlines
 */

#include "xgl_transport_internal.h"
#include "xgl_transport_rx_internal.h"
#include <internal/xgl_diagnostics.h>

/** \brief           Start at the rotating owner only during bounded work. */
static xgl_transport_peer_state_t*
transport_first_work_peer(const xgl_transport_ctx_t* ctx) {
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (ctx->work != NULL) {
        return ctx->work->first;
    }
#endif
    return ctx->peers;
}

/** \brief           Visit every owner once, wrapping only in bounded mode. */
static xgl_transport_peer_state_t*
transport_next_work_peer(const xgl_transport_ctx_t* ctx,
                         const xgl_transport_peer_state_t* peer) {
    xgl_transport_peer_state_t* next = peer->next;
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (ctx->work != NULL) {
        if (next == NULL) {
            next = ctx->peers;
        }
        if (next == ctx->work->first) {
            next = NULL;
        }
    }
#else
    (void)ctx;
#endif
    return next;
}

/**
 * \brief           Retry an owned packet or fail its peer at the retry limit
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Owning peer
 * \param[in,out]   rel_packet: Packet retained until ACK or peer failure
 * \param[in]       current_time_ms: Current transport clock
 * \return          Send result or XGL_ERR_ACK_TIMEOUT at the retry limit
 */
static xgl_error_t transport_retransmit_reliable_packet(
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

    if (!transport_tx_budget_available(ctx, peer)) {
        return XGL_OK;
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
                           .extensions_len = rel_packet->extensions_len};

    xgl_error_t err = transport_submit_packet(ctx, handle, &packet);
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
    rel_packet->fast_retry_pending = false;
    rel_packet->retry_count++;
    rel_packet->timeout_ms = xgl_reliable_calc_backoff(
        rel_packet->initial_timeout_ms, rel_packet->retry_count);
    rel_packet->send_timestamp = current_time_ms;
    rel_packet->sent = true;
#if XGL_FEATURE_STATISTICS
    if (ctx->tx_retries != NULL) {
        (*ctx->tx_retries)++;
    }
#endif

    return XGL_OK;
}

/**
 * \brief           Retry due packets of one peer
 * \param[in,out]   ctx: Transport layer context
 * \param[in,out]   peer: Owning peer
 * \param[in]       handle: Protocol instance handle
 * \param[in]       current_time_ms: Current transport clock
 * \return          First terminal retry error; retained backpressure is normal
 */
static xgl_error_t transport_process_retransmission_queue(
    xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    xgl_handle_t handle, uint32_t current_time_ms) {
    xgl_reliable_queue_t* queue = &peer->reliable_queue;
    xgct_list_node_t* node;
    xgct_list_node_t* tmp;
    xgl_error_t first_error = XGL_OK;

    XGCT_LIST_FOR_EACH_SAFE(&queue->wait_ack_list, node, tmp) {
        xgl_reliable_packet_t* rel_packet =
            /* Intrusive node membership is established by the owning list. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);

        if (!rel_packet->sent) {
            continue;
        }

        uint32_t elapsed_ms = current_time_ms - rel_packet->send_timestamp;
        if (rel_packet->retry_pending) {
            if (current_time_ms - rel_packet->retry_started_ms <
                transport_retry_delay_ms(ctx)) {
                continue;
            }
        } else if (!rel_packet->fast_retry_pending &&
                   elapsed_ms < (uint32_t)rel_packet->timeout_ms) {
            continue;
        }

        xgl_error_t error = transport_retransmit_reliable_packet(
            ctx, handle, peer, rel_packet, current_time_ms);
        if (error != XGL_OK && error != XGL_ERR_BUSY &&
            error != XGL_ERR_NO_MEMORY && first_error == XGL_OK) {
            first_error = error;
        }
        /* Failure clears the entire queue. */
        if (peer->failed) {
            break;
        }
    }
    return first_error;
}

/**
 * \brief           Process each peer-owned retransmission queue
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       current_time_ms: Current transport clock
 * \return          First terminal error while continuing other peer queues
 */
static xgl_error_t transport_process_retransmissions(xgl_transport_ctx_t* ctx,
                                                     xgl_handle_t handle,
                                                     uint32_t current_time_ms) {
    xgl_error_t first_error = XGL_OK;
    for (xgl_transport_peer_state_t* peer = transport_first_work_peer(ctx);
         peer != NULL; peer = transport_next_work_peer(ctx, peer)) {
        xgl_error_t error = transport_process_retransmission_queue(
            ctx, peer, handle, current_time_ms);
        if (first_error == XGL_OK) {
            first_error = error;
        }
    }
    return first_error;
}

#if XGL_FEATURE_FRAGMENTATION
/** \brief           Current maintenance ownership for detached expirations. */
typedef struct {
    xgl_transport_ctx_t* ctx;
    xgl_handle_t handle;
    uint32_t unreliable_timeouts;
} transport_fragment_expiration_t;

/**
 * \brief           Fail only scopes owning admitted reliable fragment bytes
 * \param[in]       buffer: Expired message detached from active reassembly
 * \param[in]       user_data: Current synchronous maintenance context
 */
static void transport_expire_fragment(const xgl_reassembly_buffer_t* buffer,
                                      void* user_data) {
    transport_fragment_expiration_t* expiration = user_data;
    if (!buffer->reliable) {
        expiration->unreliable_timeouts++;
        return;
    }
    xgl_transport_peer_state_t* peer =
        transport_find_peer_scope(expiration->ctx, buffer->source_id,
                                  buffer->connection_id, buffer->session_epoch);
    if (peer != NULL && !peer->failed) {
        transport_fail_peer(expiration->ctx, expiration->handle, peer,
                            XGL_ERR_TIMEOUT);
    }
}
#endif

/**
 * \brief           Periodic transport layer processing
 */
xgl_error_t xgl_transport_run(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                              uint32_t current_time_ms) {
    if (!ctx) {
        return XGL_ERR_NULL_POINTER;
    }

    ctx->current_time_ms = current_time_ms;
    xgl_error_t first_error = XGL_OK;
#if XGL_FEATURE_OUT_OF_ORDER
    xgl_error_t transient_feedback_error = XGL_OK;
#endif
#if XGL_FEATURE_FRAGMENTATION || XGL_FEATURE_OUT_OF_ORDER ||                   \
    XGL_FEATURE_ADVANCED_RUNTIME
    for (xgl_transport_peer_state_t* peer = transport_first_work_peer(ctx);
         peer != NULL; peer = transport_next_work_peer(ctx, peer)) {
        if (peer->failed) {
            continue;
        }
#if XGL_FEATURE_ADVANCED_RUNTIME
        if (transport_expire_tx(ctx, handle, peer)) {
            if (first_error == XGL_OK) {
                first_error = XGL_ERR_TIMEOUT;
            }
            continue;
        }
#endif
#if XGL_FEATURE_FRAGMENTATION
        xgl_error_t error = transport_pump_tx_message(ctx, handle, peer);
        if (first_error == XGL_OK) {
            first_error = error;
        }
        if (peer->failed) {
            continue;
        }
        error = transport_drain_pending_message(ctx, handle, peer);
        if (error != XGL_OK) {
            if (error != XGL_ERR_BUSY && error != XGL_ERR_NO_MEMORY &&
                first_error == XGL_OK) {
                first_error = error;
            }
            continue;
        }
#endif
#if XGL_FEATURE_OUT_OF_ORDER
        bool retained_backpressure = false;
        xgl_error_t delivery_error = transport_drain_rx_buffered(
            ctx, handle, peer, &retained_backpressure);
        if (!retained_backpressure && delivery_error != XGL_OK) {
            if (delivery_error == XGL_ERR_BUSY ||
                delivery_error == XGL_ERR_NO_MEMORY) {
                /* Failed feedback owns no retry work; report it unless a hard
                 * error takes precedence during this maintenance pass. */
                if (transient_feedback_error == XGL_OK) {
                    transient_feedback_error = delivery_error;
                }
            } else if (first_error == XGL_OK) {
                first_error = delivery_error;
            }
        }
#endif
    }
#endif

    xgl_error_t retry_error =
        transport_process_retransmissions(ctx, handle, current_time_ms);
    if (first_error == XGL_OK) {
        first_error = retry_error;
    }

#if XGL_FEATURE_FRAGMENTATION
    if (ctx->fragment_mgr) {
        transport_fragment_expiration_t expiration = {ctx, handle, 0U};
        uint32_t timeout_count = xgl_fragment_process_timeouts_scoped(
            ctx->fragment_mgr, current_time_ms, transport_expire_fragment,
            &expiration);
        if (timeout_count > 0) {
            if (first_error == XGL_OK) {
                first_error = XGL_ERR_TIMEOUT;
            }
            if (expiration.unreliable_timeouts > 0U && ctx->error_callback) {
                ctx->error_callback(
                    handle, XGL_ERR_TIMEOUT,
                    XGL_ERROR_MESSAGE("Fragment reassembly timeout"),
                    ctx->callback_user_data);
            }

#if XGL_FEATURE_STATISTICS
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped += timeout_count;
            }
#endif
        }
    }

#endif

    /* Reclaim idle peer states */
    (void)transport_reclaim_idle_peers(ctx, current_time_ms);

#if XGL_FEATURE_OUT_OF_ORDER
    if (first_error == XGL_OK) {
        return transient_feedback_error;
    }
#endif
    return first_error;
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
#if XGL_FEATURE_FRAGMENTATION
    const bool has_tx_capacity =
        transport_tx_packet_count(ctx) < ctx->max_tx_packets;
#endif
    for (const xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
#if XGL_FEATURE_ADVANCED_RUNTIME
        if (peer->tx_busy && ctx->tx_lifetime_ms != 0U) {
            transport_take_timeout(now_ms, peer->tx_started_ms,
                                   ctx->tx_lifetime_ms, &active, timeout_ms);
        }
#endif
        xgct_list_node_t* node;
        XGCT_LIST_FOR_EACH(&peer->reliable_queue.wait_ack_list, node) {
            const xgl_reliable_packet_t* packet =
                /* Intrusive node membership is established by the owning list.
                 */
                /* NOLINTNEXTLINE(bugprone-casting-through-void) */
                XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
            uint32_t start;
            uint32_t interval;
            if (packet->retry_pending) {
                start = packet->retry_started_ms;
                interval = transport_retry_delay_ms(ctx);
            } else if (packet->fast_retry_pending) {
                active = true;
                *timeout_ms = 0U;
                continue;
            } else if (packet->sent && packet->timeout_ms > 0) {
                start = packet->send_timestamp;
                interval = (uint32_t)packet->timeout_ms;
            } else {
                continue;
            }
            transport_take_timeout(now_ms, start, interval, &active,
                                   timeout_ms);
        }
#if XGL_FEATURE_FRAGMENTATION
        if (peer->tx_message.data != NULL && peer->tx_message_retry_pending) {
            transport_take_timeout(now_ms, peer->tx_message_retry_started_ms,
                                   transport_retry_delay_ms(ctx), &active,
                                   timeout_ms);
        } else if (peer->tx_message.data != NULL && !peer->failed &&
                   xgl_window_can_send_packet_number(&peer->tx_window) &&
                   has_tx_capacity) {
            active = true;
            *timeout_ms = 0U;
        }
#endif
        uint32_t reclaim_timeout;
        if (transport_peer_reclaim_timeout(ctx, peer, now_ms,
                                           &reclaim_timeout)) {
            if (!active || reclaim_timeout < *timeout_ms) {
                *timeout_ms = reclaim_timeout;
            }
            active = true;
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

#if XGL_FEATURE_ADVANCED_RUNTIME
/* Current time precedes the per-step submission count. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/** \brief           Advance maintenance with an explicit submission quota. */
xgl_error_t xgl_transport_run_bounded(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle, uint32_t now_ms,
                                      size_t tx_budget, bool* pending) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (ctx == NULL || pending == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *pending = false;
    if (ctx->work != NULL) {
        return XGL_ERR_BUSY;
    }
    xgl_transport_peer_state_t* first =
        ctx->maintenance_cursor != NULL ? ctx->maintenance_cursor : ctx->peers;
    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        peer->work_blocked = false;
    }
    xgl_transport_work_t work = {tx_budget, first};
    ctx->work = &work;
    ctx->maintenance_cursor =
        first != NULL && first->next != NULL ? first->next : ctx->peers;
    xgl_error_t error = xgl_transport_run(ctx, handle, now_ms);
    ctx->work = NULL;
    /* Later terminal maintenance can release work blocked earlier this pass.
     * Only surviving owners retain a reason for immediate budget continuation.
     */
    for (const xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        if (peer->work_blocked) {
            *pending = true;
            break;
        }
    }
    return error;
}
#endif
