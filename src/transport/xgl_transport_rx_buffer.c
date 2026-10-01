/**
 * \file            xgl_transport_rx_buffer.c
 * \brief           Owned out-of-order packet storage
 */

#include "xgen/memory/allocator.h"
#include "xgl_transport_internal.h"
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

    xgm_free(ctx->memory.rx_payload, buffered->data);
    xgm_free(ctx->memory.rx_extensions, buffered->extensions);
    xgm_free(ctx->memory.rx_packet, buffered);
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
    buffered->data =
        (uint8_t*)xgm_alloc(ctx->memory.rx_payload, packet->data->data_len);
    if (buffered->data == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memcpy(buffered->data, packet->data->data, packet->data->data_len);
    buffered->data_len = packet->data->data_len;

    if (packet->extensions != NULL && packet->extensions_len > 0U) {
        buffered->extensions = (uint8_t*)xgm_alloc(ctx->memory.rx_extensions,
                                                   packet->extensions_len);
        if (buffered->extensions == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
        memcpy(buffered->extensions, packet->extensions,
               packet->extensions_len);
        buffered->extensions_len = packet->extensions_len;
    }

    buffered->packet = *packet;
    buffered->packet.packet_number = packet_number;
    buffered->packet_data.data_len = buffered->data_len;
    buffered->packet_data.data = buffered->data;
    buffered->packet.data = &buffered->packet_data;
    buffered->packet.extensions = buffered->extensions;
    buffered->packet.extensions_len = buffered->extensions_len;

    return XGL_OK;
}

static void
transport_insert_rx_buffered(xgl_transport_peer_state_t* peer,
                             xgl_transport_rx_buffered_packet_t* buffered,
                             uint32_t packet_number) {
    xgl_transport_rx_buffered_packet_t* prev = NULL;
    (void)transport_find_rx_buffered(peer, packet_number, &prev);
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
    if (transport_find_rx_buffered(peer, packet_number, NULL) != NULL) {
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
        (xgl_transport_rx_buffered_packet_t*)xgm_alloc(ctx->memory.rx_packet,
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

    transport_insert_rx_buffered(peer, buffered, packet_number);

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
