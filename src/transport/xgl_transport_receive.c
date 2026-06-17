/**
 * \file            xgl_transport_receive.c
 * \brief           Transport receive path implementation
 */

#include "xgl_transport_internal.h"

static xgl_transport_peer_state_t *
transport_find_rx_peer(xgl_transport_ctx_t *ctx, const xgl_packet_t *packet)
{
    bool scoped = (packet->connection_id != 0U || packet->session_epoch != 0U);
    if (scoped) {
        return transport_find_peer_scope(ctx, packet->source_id,
                                         packet->connection_id,
                                         packet->session_epoch);
    }

    return transport_find_peer(ctx, packet->source_id);
}

static xgl_transport_peer_state_t *
transport_get_or_create_rx_peer(xgl_transport_ctx_t *ctx,
                                const xgl_packet_t *packet)
{
    bool scoped = (packet->connection_id != 0U || packet->session_epoch != 0U);
    if (scoped) {
        return transport_get_or_create_peer_scope(ctx, packet->source_id,
                                                  packet->connection_id,
                                                  packet->session_epoch);
    }

    return transport_get_or_create_peer(ctx, packet->source_id);
}

static xgl_error_t transport_prepare_rx_peer(xgl_transport_ctx_t *ctx,
                                             const xgl_packet_t *packet,
                                             xgl_transport_peer_state_t **peer)
{
    *peer = NULL;

    if (packet->session_id == 0U) {
        return XGL_OK;
    }

    *peer = transport_find_rx_peer(ctx, packet);
    if (*peer == NULL) {
        *peer = transport_get_or_create_rx_peer(ctx, packet);
        if (*peer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
        transport_reset_peer_state(ctx, *peer, packet->session_id,
                                   packet->connection_id,
                                   packet->session_epoch);
        return XGL_OK;
    }

    if (packet->session_id != (*peer)->session_id) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    return XGL_OK;
}

static xgl_error_t transport_process_reliable_rx_order(
    xgl_transport_ctx_t *ctx, xgl_handle_t handle, const xgl_packet_t *packet,
    xgl_transport_peer_state_t **peer)
{
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
        (void) transport_send_ack(ctx, handle, packet_number, packet->source_id,
                                  packet->session_id, packet->connection_id,
                                  packet->session_epoch);
        return XGL_OK;
    }

    if (packet_number > (*peer)->rx_next_packet_number) {
        uint32_t expected_packet_number = (*peer)->rx_next_packet_number;
        xgl_error_t err = transport_cache_out_of_order_packet(
            ctx, *peer, packet, packet_number);
        (void) transport_send_sack(
            ctx, handle, *peer, packet->source_id, expected_packet_number,
            packet->session_id, packet->connection_id, packet->session_epoch);
        if (err != XGL_OK && ctx->stats != NULL) {
            ctx->stats->rx_dropped++;
        }
        return err;
    }

    (void) transport_send_ack(ctx, handle, packet_number, packet->source_id,
                              packet->session_id, packet->connection_id,
                              packet->session_epoch);
    return XGL_OK;
}

xgl_error_t xgl_transport_receive(xgl_transport_ctx_t *ctx, xgl_handle_t handle,
                                  const xgl_packet_t *packet)
{
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (packet->packet_type == XGL_PACKET_TYPE_CONTROL) {
        return transport_process_control_packet(ctx, packet);
    }

    if (packet->packet_type == XGL_PACKET_TYPE_ACK ||
        packet->reliable == XGL_RELIABILITY_ACK_ONLY) {
        return transport_process_ack_packet(ctx, handle, packet);
    }

    const uint8_t *data = NULL;
    size_t data_len = 0U;
    if (packet->data != NULL) {
        data = packet->data->data;
        data_len = packet->data->data_len;
    }
    if (data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_transport_peer_state_t *rx_peer = NULL;
    xgl_error_t err = transport_prepare_rx_peer(ctx, packet, &rx_peer);
    if (err != XGL_OK) {
        return err;
    }

    if (packet->reliable == XGL_RELIABILITY_ACK_ELICITING) {
        err =
            transport_process_reliable_rx_order(ctx, handle, packet, &rx_peer);
        if (err != XGL_OK) {
            return err;
        }

        uint32_t packet_number = transport_receive_packet_number(packet);
        if (rx_peer != NULL && packet_number < rx_peer->rx_next_packet_number) {
            return XGL_OK;
        }
        if (rx_peer != NULL && packet_number > rx_peer->rx_next_packet_number) {
            return XGL_OK;
        }
    }

    err = transport_deliver_packet(ctx, handle, packet, data, data_len);
    if (err != XGL_OK) {
        return err;
    }

    if (packet->reliable == XGL_RELIABILITY_ACK_ELICITING && rx_peer != NULL) {
        uint32_t packet_number = transport_receive_packet_number(packet);
        rx_peer->rx_next_packet_number = packet_number + 1U;
        return transport_drain_rx_buffered(ctx, handle, rx_peer);
    }

    return XGL_OK;
}
