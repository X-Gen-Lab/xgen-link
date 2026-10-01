/**
 * \file            xgl_transport_retransmit.c
 * \brief           Transport retransmission processing
 */

#include "xgl_transport_internal.h"

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
uint32_t transport_process_retransmissions(xgl_transport_ctx_t* ctx,
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
