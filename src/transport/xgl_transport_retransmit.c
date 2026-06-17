/**
 * \file            xgl_transport_retransmit.c
 * \brief           Transport retransmission processing
 */

#include "xgl_transport_internal.h"
xgl_error_t transport_retransmit_reliable_packet(
    xgl_transport_ctx_t *ctx, xgl_handle_t handle,
    xgl_reliable_packet_t *rel_packet, uint32_t current_time_ms)
{
    if (ctx == NULL || rel_packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_packet_data_t packet_data = {.ref_count = 1,
                                     .data_len = rel_packet->data_len,
                                     .data = rel_packet->data,
                                     .owned_data = NULL};

    xgl_packet_t packet = {.source_id = rel_packet->source_id,
                           .target_id = rel_packet->target_id,
                           .session_id = rel_packet->session_id,
                           .connection_id = rel_packet->connection_id,
                           .packet_number = rel_packet->packet_number,
                           .session_epoch = rel_packet->session_epoch,
                           .packet_type = rel_packet->packet_type,
                           .flags = rel_packet->flags,
                           .data_type = rel_packet->data_type,
                           .reliable = true,
                           .fragment = rel_packet->fragment,
                           .priority = rel_packet->priority,
                           .data = &packet_data,
                           .extensions = rel_packet->extensions,
                           .extensions_len = rel_packet->extensions_len,
                           .phy = rel_packet->phy};

    xgl_error_t err = xgl_layer_send(ctx->lower_layer, handle, &packet);
    if (err != XGL_OK) {
        transport_count_send_error(ctx);
        return err;
    }

    rel_packet->retry_count++;
    rel_packet->timeout_ms = xgl_reliable_calc_backoff(
        rel_packet->initial_timeout_ms, rel_packet->retry_count);
    rel_packet->send_timestamp = current_time_ms;

    return XGL_OK;
}

static uint32_t transport_process_retransmission_queue(
    xgl_transport_ctx_t *ctx, xgl_reliable_queue_t *queue, xgl_handle_t handle,
    uint32_t current_time_ms)
{
    uint32_t retransmit_count = 0;
    xgl_list_node_t *node;
    xgl_list_node_t *tmp;

    XGL_LIST_FOR_EACH_SAFE(&queue->wait_ack_list, node, tmp)
    {
        xgl_reliable_packet_t *rel_packet =
            XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);

        if (rel_packet->send_timestamp == 0U) {
            continue;
        }

        uint32_t elapsed_ms = current_time_ms - rel_packet->send_timestamp;
        if (elapsed_ms < (uint32_t) rel_packet->timeout_ms) {
            continue;
        }

        if (rel_packet->retry_count >= queue->max_retry_count) {
            uint16_t target_id = rel_packet->target_id;

            (void) xgl_reliable_remove_packet_number(
                queue, rel_packet->packet_number, target_id);
            if (ctx->error_callback != NULL) {
                ctx->error_callback(handle, XGL_ERR_ACK_TIMEOUT,
                                    "Packet retry count exhausted",
                                    ctx->callback_user_data);
            }
            transport_count_send_error(ctx);
            continue;
        }

        if (transport_retransmit_reliable_packet(ctx, handle, rel_packet,
                                                 current_time_ms) == XGL_OK) {
            retransmit_count++;
        }
    }

    return retransmit_count;
}

uint32_t transport_process_retransmissions(xgl_transport_ctx_t *ctx,
                                           xgl_handle_t handle,
                                           uint32_t current_time_ms)
{
    uint32_t retransmit_count = 0;

    for (xgl_transport_peer_state_t *peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        retransmit_count += transport_process_retransmission_queue(
            ctx, &peer->reliable_queue, handle, current_time_ms);
    }

    return retransmit_count;
}
