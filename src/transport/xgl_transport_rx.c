/**
 * \file            xgl_transport_rx.c
 * \brief           Receive classification, ordering and contiguous delivery
 */

#include "xgl/xgl_config.h"
#include "xgl_transport_internal.h"

#if XGL_FEATURE_OUT_OF_ORDER
/**
 * \brief           Deliver contiguous owned packets without discarding busy
 *                  data
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Owning receive peer
 * \return          XGL_OK after draining, or the first admission error
 */
xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t* ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t* peer) {
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    for (;;) {
        xgl_transport_rx_buffered_packet_t* buffered = peer->rx_buffered;
        if (buffered == NULL ||
            buffered->packet.packet_number != peer->rx_next_packet_number) {
            return XGL_OK;
        }

        uint32_t packet_number = buffered->packet.packet_number;
        xgl_error_t err = transport_deliver_packet(
            ctx, handle, &buffered->packet, buffered->data, buffered->data_len);
        if (err != XGL_OK) {
            /* Already advertised through SACK: retain it without depending on
             * another sender retransmission when the application is busy. */
            if (err != XGL_ERR_BUSY && err != XGL_ERR_NO_MEMORY) {
                transport_fail_peer(ctx, handle, peer, err);
            }
            return err;
        }
        (void)transport_send_ack(
            ctx, handle, packet_number, buffered->packet.source_id,
            buffered->packet.connection_id, buffered->packet.session_epoch);
        (void)transport_take_rx_buffered(peer, packet_number);
        transport_free_rx_buffered_packet(ctx, buffered);
        peer->rx_next_packet_number = packet_number + 1U;
    }
}
#endif

typedef enum {
    TRANSPORT_RX_HANDLED,
    TRANSPORT_RX_DELIVER
} transport_rx_decision_t;

/**
 * \brief           Classify duplicates and retain supported out-of-order
 *                  packets
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Borrowed reliable packet view
 * \param[in,out]   peer: Receive peer, created on first reliable packet
 * \param[out]      decision: Whether the caller must attempt payload delivery
 * \return          XGL_OK or a receive admission error
 */
static xgl_error_t transport_process_reliable_rx_order(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle, const xgl_packet_t* packet,
    xgl_transport_peer_state_t** peer, transport_rx_decision_t* decision) {
    *decision = TRANSPORT_RX_HANDLED;
    if (*peer == NULL) {
        *peer = transport_get_or_create_rx_peer(ctx, packet);
        if (*peer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
    }

    uint32_t packet_number = packet->packet_number;
    if (!(*peer)->rx_has_packet_number_state) {
        (*peer)->rx_next_packet_number = 0U;
        (*peer)->rx_has_packet_number_state = true;
    }

    if (packet_number < (*peer)->rx_next_packet_number) {
        (void)transport_send_ack(ctx, handle, packet_number, packet->source_id,
                                 packet->connection_id, packet->session_epoch);
        return XGL_OK;
    }

    if (packet_number > (*peer)->rx_next_packet_number) {
#if XGL_FEATURE_OUT_OF_ORDER
        uint32_t expected_packet_number = (*peer)->rx_next_packet_number;
        xgl_error_t err = transport_cache_out_of_order_packet(
            ctx, *peer, packet, packet_number);
        (void)transport_send_sack(ctx, handle, *peer, packet->source_id,
                                  expected_packet_number, packet->connection_id,
                                  packet->session_epoch);
#if XGL_FEATURE_STATISTICS
        if (err != XGL_OK && ctx->stats != NULL) {
            ctx->stats->rx_dropped++;
        }
#endif
        return err;
#else
        return XGL_ERR_SEQUENCE_ERROR;
#endif
    }

    /* The in-order packet is acknowledged only after delivery/admission. */
    *decision = TRANSPORT_RX_DELIVER;
    return XGL_OK;
}

/**
 * \brief           Dispatch peer traffic and acknowledge only admitted payloads
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Borrowed packet view
 * \return          XGL_OK or a validation, admission or session error
 */
xgl_error_t xgl_transport_receive(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                  const xgl_packet_t* packet) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (packet->packet_type == XGL_PACKET_TYPE_CONTROL) {
        return transport_process_control_packet(ctx, handle, packet);
    }

    if (packet->packet_type == XGL_PACKET_TYPE_ACK ||
        packet->reliable == XGL_RELIABILITY_ACK_ONLY) {
        return transport_process_ack_packet(ctx, handle, packet);
    }

    const uint8_t* data = NULL;
    size_t data_len = 0U;
    if (packet->data != NULL) {
        data = packet->data->data;
        data_len = packet->data->data_len;
    }
    if (data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

#if XGL_FEATURE_FRAGMENTATION
    if (ctx->enable_fragmentation && data_len > ctx->max_message_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
#endif

    xgl_transport_peer_state_t* rx_peer = transport_find_rx_peer(ctx, packet);
    if (rx_peer != NULL && rx_peer->failed) {
        return XGL_ERR_ACK_TIMEOUT;
    }
    xgl_error_t err;

    if (rx_peer != NULL) {
        rx_peer->last_active_ms = transport_now(ctx);
    }
    if (packet->fragment) {
#if XGL_FEATURE_FRAGMENTATION
        if (ctx->fragment_mgr == NULL) {
            return XGL_ERR_INVALID_FRAME;
        }
#else
        return XGL_ERR_INVALID_FRAME;
#endif
    }

    if (packet->reliable == XGL_RELIABILITY_ACK_ELICITING) {
        transport_rx_decision_t decision;
        err = transport_process_reliable_rx_order(ctx, handle, packet, &rx_peer,
                                                  &decision);
        if (err != XGL_OK || decision == TRANSPORT_RX_HANDLED) {
            return err;
        }
    }

#if XGL_FEATURE_OUT_OF_ORDER
    /* A cached packet is already our responsibility; retry its stored copy. */
    if (rx_peer != NULL && rx_peer->rx_buffered != NULL &&
        rx_peer->rx_buffered->packet.packet_number ==
            rx_peer->rx_next_packet_number &&
        packet->reliable == XGL_RELIABILITY_ACK_ELICITING) {
        return transport_drain_rx_buffered(ctx, handle, rx_peer);
    }

#endif

    const uint8_t* deliver_data = data;
    size_t deliver_data_len = data_len;

    err = transport_deliver_packet(ctx, handle, packet, deliver_data,
                                   deliver_data_len);
    if (err != XGL_OK) {
        return err;
    }

    if (packet->reliable == XGL_RELIABILITY_ACK_ELICITING && rx_peer != NULL) {
        uint32_t packet_number = packet->packet_number;
        rx_peer->rx_next_packet_number = packet_number + 1U;
        (void)transport_send_ack(ctx, handle, packet_number, packet->source_id,
                                 packet->connection_id, packet->session_epoch);
#if XGL_FEATURE_OUT_OF_ORDER
        return transport_drain_rx_buffered(ctx, handle, rx_peer);
#endif
    }

    return XGL_OK;
}
