/**
 * \file            xgl_transport_rx_order.c
 * \brief           Transport receive ordering and delivery
 */

#include "xgl_transport_internal.h"
#include "xgl/internal/xgl_wire.h"
#include <string.h>
static xgl_transport_rx_buffered_packet_t* transport_find_rx_buffered(
    xgl_transport_peer_state_t* peer,
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

xgl_error_t transport_cache_out_of_order_packet(xgl_transport_ctx_t* ctx,
                                                       xgl_transport_peer_state_t* peer,
                                                       const xgl_packet_t* packet,
                                                       uint32_t packet_number) {
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

    xgl_transport_rx_buffered_packet_t* buffered =
        (xgl_transport_rx_buffered_packet_t*)transport_malloc(ctx->allocator,
                                                             sizeof(*buffered));
    if (buffered == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(buffered, 0, sizeof(*buffered));

    buffered->data = (uint8_t*)transport_malloc(ctx->allocator, packet->data->data_len);
    if (buffered->data == NULL) {
        transport_free_rx_buffered_packet(ctx, buffered);
        return XGL_ERR_NO_MEMORY;
    }
    memcpy(buffered->data, packet->data->data, packet->data->data_len);
    buffered->data_len = packet->data->data_len;

    if (packet->extensions != NULL && packet->extensions_len > 0U) {
        buffered->extensions = (uint8_t*)transport_malloc(ctx->allocator,
                                                          packet->extensions_len);
        if (buffered->extensions == NULL) {
            transport_free_rx_buffered_packet(ctx, buffered);
            return XGL_ERR_NO_MEMORY;
        }
        memcpy(buffered->extensions, packet->extensions, packet->extensions_len);
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

    return XGL_OK;
}

xgl_error_t transport_deliver_packet(xgl_transport_ctx_t* ctx,
                                            xgl_handle_t handle,
                                            const xgl_packet_t* packet,
                                            const uint8_t* data,
                                            size_t data_len) {
    if (ctx == NULL || packet == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint16_t source_id = packet->source_id;
    uint8_t data_type = packet->data_type;

    if (packet->fragment && ctx->fragment_mgr != NULL) {
        uint8_t* complete_data = NULL;
        size_t complete_len = 0;
        uint32_t message_id = 0U;
        uint32_t fragment_offset = 0U;
        uint32_t message_len = 0U;
        bool has_fragment_ext = false;

        if (packet->extensions != NULL && packet->extensions_len > 0U) {
            xgl_wire_ext_cursor_t cursor;
            xgl_error_t err = xgl_wire_ext_cursor_init(&cursor,
                                                       packet->extensions,
                                                       packet->extensions_len);
            if (err != XGL_OK) {
                return err;
            }

            xgl_wire_ext_t ext;
            while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
                if (ext.type == XGL_WIRE_EXT_FRAGMENT) {
                    err = xgl_wire_decode_fragment_ext_value(ext.value,
                                                             ext.len,
                                                             &message_id,
                                                             &fragment_offset,
                                                             &message_len);
                    if (err != XGL_OK) {
                        return err;
                    }
                    has_fragment_ext = true;
                    break;
                }
            }
            if (err != XGL_OK && err != XGL_ERR_NOT_FOUND) {
                return err;
            }
        }

        if (!has_fragment_ext) {
            return XGL_ERR_INVALID_FRAME;
        }

        xgl_error_t err = xgl_fragment_process_ext(ctx->fragment_mgr,
                                                   source_id,
                                                   packet->connection_id,
                                                   packet->session_epoch,
                                                   data_type,
                                                   message_id,
                                                   fragment_offset,
                                                   message_len,
                                                   data,
                                                   data_len,
                                                   &complete_data,
                                                   &complete_len,
                                                   0);

        if (err == XGL_OK) {
            if (ctx->rx_callback != NULL) {
                ctx->rx_callback(handle, source_id, data_type,
                                 complete_data, complete_len, ctx->callback_user_data);
            }
            xgl_fragment_free_data(ctx->fragment_mgr, complete_data);
            if (ctx->stats != NULL) {
                ctx->stats->rx_packets++;
                ctx->stats->rx_bytes += complete_len;
            }
        } else if (err == XGL_ERR_BUSY) {
            return XGL_OK;
        } else {
            return err;
        }
    } else {
        if (ctx->rx_callback != NULL) {
            ctx->rx_callback(handle, source_id, data_type,
                             data, data_len, ctx->callback_user_data);
        }

        if (ctx->stats != NULL) {
            ctx->stats->rx_packets++;
            ctx->stats->rx_bytes += data_len;
        }
    }

    return XGL_OK;
}

xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t* ctx,
                                               xgl_handle_t handle,
                                               xgl_transport_peer_state_t* peer) {
    if (ctx == NULL || peer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    for (;;) {
        xgl_transport_rx_buffered_packet_t* prev = NULL;
        xgl_transport_rx_buffered_packet_t* buffered =
            transport_find_rx_buffered(peer, peer->rx_next_packet_number, &prev);
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
        xgl_error_t err = transport_deliver_packet(ctx,
                                                   handle,
                                                   &buffered->packet,
                                                   buffered->data,
                                                   buffered->data_len);
        (void)transport_send_ack(ctx,
                                 handle,
                                 packet_number,
                                 buffered->packet.source_id,
                                 buffered->packet.session_id,
                                 buffered->packet.connection_id,
                                 buffered->packet.session_epoch);
        transport_free_rx_buffered_packet(ctx, buffered);
        if (err != XGL_OK) {
            return err;
        }
        peer->rx_next_packet_number = packet_number + 1U;
    }
}
