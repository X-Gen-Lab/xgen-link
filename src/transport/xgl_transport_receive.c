/**
 * \file            xgl_transport_receive.c
 * \brief           Transport receive path implementation
 */

#include "xgl_transport_internal.h"
#include "xgl/internal/xgl_time.h"
#include "xgl/internal/xgl_codec.h"
#include "xgl/xgl_config.h"

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

    if (rx_peer != NULL) {
        rx_peer->last_active_ms = xgl_time_ms();
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

    /* Apply codec decompression before delivery if packet is compressed */
    const uint8_t *deliver_data = data;
    size_t deliver_data_len = data_len;
    uint8_t decode_buffer[XGL_DATALINK_STACK_BUFFER_SIZE];

    if (packet->compress != 0U && ctx->codec_registry != NULL) {
        const xgl_codec_t *codec = xgl_codec_find(
            ctx->codec_registry, XGL_CODEC_KIND_COMPRESSION, packet->compress);
        if (codec != NULL) {
            size_t decoded_len = sizeof(decode_buffer);
            err = codec->decode(data, data_len, decode_buffer, &decoded_len,
                                codec->user_data);
            if (err == XGL_OK) {
                deliver_data = decode_buffer;
                deliver_data_len = decoded_len;
            }
            /* If decode fails, fall through with original (compressed) data */
        }
    }

    err = transport_deliver_packet(ctx, handle, packet, deliver_data, deliver_data_len);
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
