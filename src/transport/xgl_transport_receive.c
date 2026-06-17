/**
 * \file            xgl_transport_receive.c
 * \brief           Transport receive path implementation
 */

#include "xgl_transport_internal.h"

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
