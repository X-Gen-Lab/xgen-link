/**
 * \file            xgl_transport_rx_order.c
 * \brief           Transport receive ordering and delivery
 */

#include <string.h>

#include "xgl_transport_internal.h"

static xgl_transport_rx_buffered_packet_t *
transport_find_rx_buffered(xgl_transport_peer_state_t *peer,
                           uint32_t packet_number,
                           xgl_transport_rx_buffered_packet_t **previous)
{
    if (previous != NULL) {
        *previous = NULL;
    }
    if (peer == NULL) {
        return NULL;
    }

    xgl_transport_rx_buffered_packet_t *prev = NULL;
    xgl_transport_rx_buffered_packet_t *node = peer->rx_buffered;
    while (node != NULL) {
        if (node->packet.packet_number == packet_number) {
            if (previous != NULL) {
                *previous = prev;
            }
            return node;
        }
        if (node->packet.packet_number > packet_number) {
            break;
        }
        prev = node;
        node = node->next;
    }

    if (previous != NULL) {
        *previous = prev;
    }
    return NULL;
}

void transport_clear_rx_buffered(xgl_transport_ctx_t *ctx,
                                 xgl_transport_peer_state_t *peer)
{
    if (peer == NULL) {
        return;
    }

    xgl_transport_rx_buffered_packet_t *node = peer->rx_buffered;
    while (node != NULL) {
        xgl_transport_rx_buffered_packet_t *next = node->next;
        transport_free_rx_buffered_packet(ctx, node);
        node = next;
    }
    peer->rx_buffered = NULL;
    peer->rx_buffered_count = 0U;
}

xgl_error_t transport_cache_out_of_order_packet(
    xgl_transport_ctx_t *ctx, xgl_transport_peer_state_t *peer,
    const xgl_packet_t *packet, uint32_t packet_number)
{
    if (ctx == NULL || peer == NULL || packet == NULL || packet->data == NULL ||
        packet->data->data == NULL || packet->data->data_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    uint32_t window = peer->tx_window.window_size;
    if (window == 0U) {
        window = 1U;
    }
    if (packet_number - peer->rx_next_packet_number >= window) {
        return XGL_ERR_WINDOW_FULL;
    }
    if (peer->rx_buffered_count >= window) {
        return XGL_ERR_WINDOW_FULL;
    }
    if (transport_find_rx_buffered(peer, packet_number, NULL) != NULL) {
        return XGL_OK;
    }

    xgl_transport_rx_buffered_packet_t *buffered =
        (xgl_transport_rx_buffered_packet_t *) transport_malloc(
            ctx->allocator, sizeof(*buffered));
    if (buffered == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(buffered, 0, sizeof(*buffered));

    buffered->data =
        (uint8_t *) transport_malloc(ctx->allocator, packet->data->data_len);
    if (buffered->data == NULL) {
        transport_free_rx_buffered_packet(ctx, buffered);
        return XGL_ERR_NO_MEMORY;
    }
    memcpy(buffered->data, packet->data->data, packet->data->data_len);
    buffered->data_len = packet->data->data_len;

    if (packet->extensions != NULL && packet->extensions_len > 0U) {
        buffered->extensions = (uint8_t *) transport_malloc(
            ctx->allocator, packet->extensions_len);
        if (buffered->extensions == NULL) {
            transport_free_rx_buffered_packet(ctx, buffered);
            return XGL_ERR_NO_MEMORY;
        }
        memcpy(buffered->extensions, packet->extensions,
               packet->extensions_len);
        buffered->extensions_len = packet->extensions_len;
    }

    buffered->packet = *packet;
    buffered->packet.packet_number = packet_number;
    buffered->packet_data.ref_count = 1;
    buffered->packet_data.data_len = buffered->data_len;
    buffered->packet_data.data = buffered->data;
    buffered->packet_data.owned_data = buffered->data;
    buffered->packet.data = &buffered->packet_data;
    buffered->packet.extensions = buffered->extensions;
    buffered->packet.extensions_len = buffered->extensions_len;

    xgl_transport_rx_buffered_packet_t *prev = NULL;
    (void) transport_find_rx_buffered(peer, packet_number, &prev);
    if (prev == NULL) {
        buffered->next = peer->rx_buffered;
        peer->rx_buffered = buffered;
    } else {
        buffered->next = prev->next;
        prev->next = buffered;
    }
    peer->rx_buffered_count++;

    return XGL_OK;
}

xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t *ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t *peer)
{
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    for (;;) {
        xgl_transport_rx_buffered_packet_t *prev = NULL;
        xgl_transport_rx_buffered_packet_t *buffered =
            transport_find_rx_buffered(peer, peer->rx_next_packet_number,
                                       &prev);
        if (buffered == NULL) {
            return XGL_OK;
        }

        if (prev == NULL) {
            peer->rx_buffered = buffered->next;
        } else {
            prev->next = buffered->next;
        }
        buffered->next = NULL;
        if (peer->rx_buffered_count > 0U) {
            peer->rx_buffered_count--;
        }

        uint32_t packet_number = buffered->packet.packet_number;
        xgl_error_t err = transport_deliver_packet(
            ctx, handle, &buffered->packet, buffered->data, buffered->data_len);
        (void) transport_send_ack(
            ctx, handle, packet_number, buffered->packet.source_id,
            buffered->packet.session_id, buffered->packet.connection_id,
            buffered->packet.session_epoch);
        transport_free_rx_buffered_packet(ctx, buffered);
        if (err != XGL_OK) {
            return err;
        }
        peer->rx_next_packet_number = packet_number + 1U;
    }
}
