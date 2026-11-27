/**
 * \file            xgl_transport_rx_buffer.c
 * \brief           Owned out-of-order packet storage
 */

#include "internal/xgl_allocator.h"
#include "xgl_transport_rx_internal.h"
#include <string.h>

#if XGL_FEATURE_OUT_OF_ORDER
/**
 * \brief           Release a buffered receive packet through its resource
 * services
 * \param[in]       ctx: Transport context
 * \param[in,out]   buffered: Owned packet storage, or NULL
 */
void transport_free_rx_buffered_packet(
    const xgl_transport_ctx_t* ctx,
    xgl_transport_rx_buffered_packet_t* buffered) {
    if (buffered == NULL) {
        return;
    }

    xgl_free(ctx->memory.rx_payload, buffered->storage);
    xgl_free(ctx->memory.rx_packet, buffered);
}

#endif

static xgl_transport_rx_buffered_packet_t*
transport_find_rx_buffered(xgl_transport_peer_state_t* peer,
                           uint32_t packet_number,
                           xgl_transport_rx_buffered_packet_t** previous) {
    if (previous != NULL) {
        *previous = NULL;
    }
    if (peer == NULL) {
        return NULL;
    }

    xgl_transport_rx_buffered_packet_t* prev = NULL;
    xgl_transport_rx_buffered_packet_t* node = peer->rx_buffered;
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

void transport_clear_rx_buffered(const xgl_transport_ctx_t* ctx,
                                 xgl_transport_peer_state_t* peer) {
    if (peer == NULL) {
        return;
    }

    xgl_transport_rx_buffered_packet_t* node = peer->rx_buffered;
    while (node != NULL) {
        xgl_transport_rx_buffered_packet_t* next = node->next;
        transport_free_rx_buffered_packet(ctx, node);
        node = next;
    }
    peer->rx_buffered = NULL;
    peer->rx_buffered_count = 0U;
}

static xgl_error_t
transport_copy_rx_buffered_packet(const xgl_transport_ctx_t* ctx,
                                  xgl_transport_rx_buffered_packet_t* buffered,
                                  const xgl_packet_t* packet,
                                  uint32_t packet_number) {
    const size_t payload_len = packet->data->data_len;
    buffered->storage =
        (uint8_t*)xgl_alloc(ctx->memory.rx_payload, payload_len);
    if (buffered->storage == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memcpy(buffered->storage, packet->data->data, payload_len);

    buffered->packet = *packet;
    buffered->packet.packet_number = packet_number;
    buffered->packet_data.data_len = payload_len;
    buffered->packet_data.data = buffered->storage;
    buffered->packet.data = &buffered->packet_data;
    /* Delivery uses the parsed fields and the owned payload. */
    buffered->packet.extensions = NULL;
    buffered->packet.extensions_len = 0U;

    return XGL_OK;
}

static void
transport_insert_rx_buffered(xgl_transport_peer_state_t* peer,
                             xgl_transport_rx_buffered_packet_t* buffered,
                             xgl_transport_rx_buffered_packet_t* prev) {
    if (prev == NULL) {
        buffered->next = peer->rx_buffered;
        peer->rx_buffered = buffered;
    } else {
        buffered->next = prev->next;
        prev->next = buffered;
    }
    peer->rx_buffered_count++;
}

xgl_error_t transport_cache_out_of_order_packet(
    const xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    const xgl_packet_t* packet, uint32_t packet_number) {
    if (ctx == NULL || peer == NULL || packet == NULL || packet->data == NULL ||
        packet->data->data == NULL || packet->data->data_len == 0U ||
        (packet->extensions == NULL && packet->extensions_len != 0U)) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Check the full input frame before retaining its payload. Subtract before
     * adding so malformed internal input cannot wrap size_t. */
    const size_t overhead = XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE;
    if (ctx->max_frame_size < overhead ||
        packet->data->data_len > ctx->max_frame_size - overhead ||
        packet->extensions_len >
            ctx->max_frame_size - overhead - packet->data->data_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint32_t window = peer->tx_window.window_size;
    if (window == 0U) {
        window = 1U;
    }
    if (packet_number - peer->rx_next_packet_number >= window) {
        return XGL_ERR_WINDOW_FULL;
    }
    xgl_transport_rx_buffered_packet_t* prev = NULL;
    if (transport_find_rx_buffered(peer, packet_number, &prev) != NULL) {
        return XGL_OK;
    }
    size_t buffered_count = 0U;
    for (const xgl_transport_peer_state_t* owner = ctx->peers; owner != NULL;
         owner = owner->next) {
        buffered_count += owner->rx_buffered_count;
    }
    if (peer->rx_buffered_count >= window ||
        buffered_count >= ctx->max_rx_buffered_packets) {
        return XGL_ERR_WINDOW_FULL;
    }

    xgl_transport_rx_buffered_packet_t* buffered =
        (xgl_transport_rx_buffered_packet_t*)xgl_alloc(ctx->memory.rx_packet,
                                                       sizeof(*buffered));
    if (buffered == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(buffered, 0, sizeof(*buffered));

    xgl_error_t err =
        transport_copy_rx_buffered_packet(ctx, buffered, packet, packet_number);
    if (err != XGL_OK) {
        transport_free_rx_buffered_packet(ctx, buffered);
        return err;
    }

    transport_insert_rx_buffered(peer, buffered, prev);

    return XGL_OK;
}

xgl_transport_rx_buffered_packet_t*
transport_take_rx_buffered(xgl_transport_peer_state_t* peer,
                           uint32_t packet_number) {
    xgl_transport_rx_buffered_packet_t* prev = NULL;
    xgl_transport_rx_buffered_packet_t* buffered =
        transport_find_rx_buffered(peer, packet_number, &prev);
    if (buffered == NULL) {
        return NULL;
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

    return buffered;
}
