/**
 * \file            xgl_transport.c
 * \brief           Transport Layer Main Interface Implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_transport.h"
#include "xgl/xgl_packet_pool.h"
#include "xgl/xgl_route.h"
#include "xgl/xgl_frame.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_time.h"
#include "xgl/xgl_wire.h"
#include <string.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using allocator
 */
static void* transport_malloc(xgl_allocator_t* allocator, size_t size) {
    if (allocator && allocator->malloc) {
        return allocator->malloc(size);
    }
    return malloc(size);
}

/**
 * \brief           Free memory using allocator
 */
static void transport_free(xgl_allocator_t* allocator, void* ptr) {
    if (allocator && allocator->free) {
        allocator->free(ptr);
    } else {
        free(ptr);
    }
}

static void transport_free_rx_buffered_packet(xgl_transport_ctx_t* ctx,
                                              xgl_transport_rx_buffered_packet_t* buffered) {
    if (buffered == NULL) {
        return;
    }

    transport_free(ctx != NULL ? ctx->allocator : NULL, buffered->data);
    transport_free(ctx != NULL ? ctx->allocator : NULL, buffered->extensions);
    transport_free(ctx != NULL ? ctx->allocator : NULL, buffered);
}

static void transport_clear_rx_buffered(xgl_transport_ctx_t* ctx,
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

static xgl_transport_peer_state_t* transport_find_peer(xgl_transport_ctx_t* ctx,
                                                       uint16_t peer_id) {
    if (ctx == NULL) {
        return NULL;
    }

    xgl_transport_peer_state_t* peer = ctx->peers;
    while (peer != NULL) {
        if (peer->peer_id == peer_id) {
            return peer;
        }
        peer = peer->next;
    }

    return NULL;
}

static xgl_transport_peer_state_t* transport_find_peer_scope(xgl_transport_ctx_t* ctx,
                                                             uint16_t peer_id,
                                                             uint32_t connection_id,
                                                             uint32_t session_epoch) {
    if (ctx == NULL) {
        return NULL;
    }

    xgl_transport_peer_state_t* peer = ctx->peers;
    while (peer != NULL) {
        if (peer->peer_id == peer_id &&
            peer->has_connection_scope &&
            peer->connection_id == connection_id &&
            peer->session_epoch == session_epoch) {
            return peer;
        }
        peer = peer->next;
    }

    return NULL;
}

static xgl_transport_peer_state_t* transport_get_or_create_peer_internal(
    xgl_transport_ctx_t* ctx,
    uint16_t peer_id,
    uint32_t connection_id,
    uint32_t session_epoch,
    bool has_connection_scope) {
    xgl_transport_peer_state_t* peer = has_connection_scope ?
        transport_find_peer_scope(ctx, peer_id, connection_id, session_epoch) :
        transport_find_peer(ctx, peer_id);
    if (peer != NULL) {
        return peer;
    }

    peer = (xgl_transport_peer_state_t*)transport_malloc(ctx->allocator,
                                                         sizeof(xgl_transport_peer_state_t));
    if (peer == NULL) {
        return NULL;
    }

    memset(peer, 0, sizeof(*peer));
    peer->peer_id = peer_id;
    peer->has_connection_scope = has_connection_scope;
    peer->connection_id = has_connection_scope ? connection_id : 0U;
    peer->session_epoch = has_connection_scope ? session_epoch : 0U;
    peer->session_id = (uint16_t)(ctx->next_session_id & XGL_SESSION_ID_MASK);
    if (peer->session_id == 0U) {
        peer->session_id = 1U;
    }
    ctx->next_session_id = (uint16_t)((peer->session_id + 1U) & XGL_SESSION_ID_MASK);
    if (ctx->next_session_id == 0U) {
        ctx->next_session_id = 1U;
    }
    xgl_rtt_init(&peer->rtt_est);

    xgl_error_t err = xgl_window_init_with_allocator(&peer->tx_window,
                                                     ctx->window.window_size,
                                                     ctx->allocator);
    if (err != XGL_OK) {
        transport_free(ctx->allocator, peer);
        return NULL;
    }

    err = xgl_reliable_init(&peer->reliable_queue,
                            ctx->max_retry_count,
                            ctx->allocator);
    if (err != XGL_OK) {
        xgl_window_destroy(&peer->tx_window);
        transport_free(ctx->allocator, peer);
        return NULL;
    }

    peer->next = ctx->peers;
    ctx->peers = peer;
    return peer;
}

static xgl_transport_peer_state_t* transport_get_or_create_peer(xgl_transport_ctx_t* ctx,
                                                                uint16_t peer_id) {
    return transport_get_or_create_peer_internal(ctx, peer_id, 0U, 0U, false);
}

static xgl_transport_peer_state_t* transport_get_or_create_peer_scope(
    xgl_transport_ctx_t* ctx,
    uint16_t peer_id,
    uint32_t connection_id,
    uint32_t session_epoch) {
    return transport_get_or_create_peer_internal(ctx,
                                                 peer_id,
                                                 connection_id,
                                                 session_epoch,
                                                 true);
}

static void transport_destroy_peers(xgl_transport_ctx_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    xgl_transport_peer_state_t* peer = ctx->peers;
    while (peer != NULL) {
        xgl_transport_peer_state_t* next = peer->next;
        transport_clear_rx_buffered(ctx, peer);
        xgl_reliable_destroy(&peer->reliable_queue);
        xgl_window_destroy(&peer->tx_window);
        transport_free(ctx->allocator, peer);
        peer = next;
    }
    ctx->peers = NULL;
}

static void transport_advance_packet_window_if_possible(xgl_transport_ctx_t* ctx) {
    if (ctx != NULL && xgl_window_can_send_packet_number(&ctx->window)) {
        xgl_window_advance_next_packet_number(&ctx->window);
    }
}

static uint32_t transport_allocate_packet_number(xgl_transport_ctx_t* ctx,
                                                 xgl_transport_peer_state_t* peer) {
    uint32_t packet_number = xgl_window_get_next_packet_number(&peer->tx_window);
    xgl_window_advance_next_packet_number(&peer->tx_window);
    transport_advance_packet_window_if_possible(ctx);

    return packet_number;
}

static uint32_t transport_receive_packet_number(const xgl_packet_t* packet) {
    if (packet == NULL) {
        return 0U;
    }

    return packet->packet_number;
}

static void transport_reset_peer_state(xgl_transport_ctx_t* ctx,
                                       xgl_transport_peer_state_t* peer,
                                       uint16_t session_id,
                                       uint32_t connection_id,
                                       uint32_t session_epoch) {
    if (ctx == NULL || peer == NULL) {
        return;
    }

    peer->session_id = (uint16_t)(session_id & XGL_SESSION_ID_MASK);
    peer->hello_sent = false;
    peer->session_established = true;
    xgl_reliable_clear(&peer->reliable_queue);
    xgl_window_reset(&peer->tx_window);
    xgl_window_reset(&ctx->window);
    if (ctx->fragment_mgr != NULL) {
        (void)xgl_fragment_clear_reassembly_scope(ctx->fragment_mgr,
                                                  peer->peer_id,
                                                  connection_id,
                                                  session_epoch);
    }
    xgl_rtt_init(&peer->rtt_est);
    peer->rx_next_packet_number = 0U;
    peer->rx_has_packet_number_state = false;
    transport_clear_rx_buffered(ctx, peer);
    peer->last_active_ms = xgl_time_ms();
}

static xgl_error_t transport_send_control(xgl_transport_ctx_t* ctx,
                                          xgl_handle_t handle,
                                          uint16_t target_id,
                                          uint8_t control_type,
                                          uint32_t control_packet_number,
                                          uint16_t session_id,
                                          uint32_t connection_id,
                                          uint32_t session_epoch) {
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = NULL,
        .owned_data = NULL
    };

    xgl_packet_t packet = {
        .source_id = ctx->local_id,
        .target_id = target_id,
        .session_id = session_id,
        .connection_id = connection_id,
        .packet_number = control_packet_number,
        .session_epoch = session_epoch,
        .data_type = control_type,
        .reliable = (control_type == XGL_TRANSPORT_CONTROL_NACK ||
                     control_type == XGL_TRANSPORT_CONTROL_SACK) ?
                    XGL_RELIABILITY_ACK_ONLY : XGL_RELIABILITY_NONE,
        .fragment = false,
        .priority = 7,
        .data = &packet_data,
        .phy = NULL
    };

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    return xgl_layer_send(ctx->lower_layer, handle, &packet);
}

static xgl_error_t transport_process_control_packet(xgl_transport_ctx_t* ctx,
                                                    const xgl_packet_t* packet) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint16_t session_id = (uint16_t)(packet->session_id & XGL_SESSION_ID_MASK);
    if (session_id == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    bool has_connection_scope =
        (packet->connection_id != 0U || packet->session_epoch != 0U);
    xgl_transport_peer_state_t* peer = has_connection_scope ?
        transport_get_or_create_peer_scope(ctx,
                                           packet->source_id,
                                           packet->connection_id,
                                           packet->session_epoch) :
        transport_get_or_create_peer(ctx, packet->source_id);
    if (peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    if (packet->data_type == XGL_TRANSPORT_CONTROL_RESET ||
        peer->session_id != session_id) {
        transport_reset_peer_state(ctx,
                                   peer,
                                   session_id,
                                   packet->connection_id,
                                   packet->session_epoch);
    } else {
        peer->session_established = true;
        peer->last_active_ms = xgl_time_ms();
    }

    return XGL_OK;
}

/**
 * \brief           Send ACK packet for received data
 * \param[in]       ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet_number: Packet number to acknowledge
 * \param[in]       source_id: Source node ID
 * \return          XGL_OK on success, error code otherwise
 */
static xgl_error_t transport_send_ack(xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     uint32_t packet_number,
                                     uint16_t source_id,
                                     uint16_t session_id) {
    uint8_t ack_value[16] = {0};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {
        {.gap = 0, .length = 1}
    };

    xgl_error_t err = xgl_wire_encode_ack_range_ext_value(ack_value,
                                                          sizeof(ack_value),
                                                          packet_number,
                                                          0,
                                                          ranges,
                                                          1,
                                                          &ack_value_len);
    if (err != XGL_OK) {
        return err;
    }

    uint8_t ack_ext[32] = {0};
    size_t ack_ext_len = 0;
    err = xgl_wire_encode_ext(ack_ext,
                              sizeof(ack_ext),
                              XGL_WIRE_EXT_ACK_RANGE,
                              ack_value,
                              ack_value_len,
                              &ack_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_packet_data_t ack_packet_data = {
        .ref_count = 1,
        .data_len = ack_ext_len,
        .data = ack_ext,
        .owned_data = NULL
    };
    
    xgl_packet_t ack_packet = {
        .source_id = ctx->local_id,
        .target_id = source_id,
        .session_id = session_id,
        .packet_number = packet_number,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .fragment = false,
        .priority = 7,
        .data = &ack_packet_data,
        .phy = NULL
    };
    
    if (ctx->lower_layer != NULL && ctx->lower_layer->send != NULL) {
        return xgl_layer_send(ctx->lower_layer, handle, &ack_packet);
    }
    
    return XGL_ERR_INVALID_PARAM;
}

static xgl_error_t transport_retransmit_reliable_packet(xgl_transport_ctx_t* ctx,
                                                        xgl_handle_t handle,
                                                        xgl_reliable_packet_t* rel_packet,
                                                        uint32_t current_time_ms) {
    if (ctx == NULL || rel_packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = rel_packet->data_len,
        .data = rel_packet->data,
        .owned_data = NULL
    };

    xgl_packet_t packet = {
        .source_id = rel_packet->source_id,
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
        .phy = rel_packet->phy
    };

    xgl_error_t err = xgl_layer_send(ctx->lower_layer, handle, &packet);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return err;
    }

    rel_packet->retry_count++;
    rel_packet->timeout_ms = xgl_reliable_calc_backoff(
        rel_packet->initial_timeout_ms,
        rel_packet->retry_count
    );
    rel_packet->send_timestamp = current_time_ms;

    return XGL_OK;
}

static uint32_t transport_process_retransmission_queue(xgl_transport_ctx_t* ctx,
                                                       xgl_reliable_queue_t* queue,
                                                       xgl_handle_t handle,
                                                       uint32_t current_time_ms) {
    uint32_t retransmit_count = 0;
    xgl_list_node_t* node;
    xgl_list_node_t* tmp;

    XGL_LIST_FOR_EACH_SAFE(&queue->wait_ack_list, node, tmp) {
        xgl_reliable_packet_t* rel_packet = XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);

        if (rel_packet->send_timestamp == 0U) {
            continue;
        }

        uint32_t elapsed_ms = current_time_ms - rel_packet->send_timestamp;
        if (elapsed_ms < (uint32_t)rel_packet->timeout_ms) {
            continue;
        }

        if (rel_packet->retry_count >= queue->max_retry_count) {
            uint16_t target_id = rel_packet->target_id;

            (void)xgl_reliable_remove_packet_number(queue,
                                                    rel_packet->packet_number,
                                                    target_id);
            if (ctx->error_callback != NULL) {
                ctx->error_callback(handle, XGL_ERR_ACK_TIMEOUT,
                                  "Packet retry count exhausted",
                                  ctx->callback_user_data);
            }
            if (ctx->stats != NULL) {
                ctx->stats->tx_errors++;
            }
            continue;
        }

        if (transport_retransmit_reliable_packet(ctx,
                                                 handle,
                                                 rel_packet,
                                                 current_time_ms) == XGL_OK) {
            retransmit_count++;
        }
    }

    return retransmit_count;
}

static uint32_t transport_process_retransmissions(xgl_transport_ctx_t* ctx,
                                                  xgl_handle_t handle,
                                                  uint32_t current_time_ms) {
    uint32_t retransmit_count = 0;

    for (xgl_transport_peer_state_t* peer = ctx->peers;
         peer != NULL;
         peer = peer->next) {
        retransmit_count += transport_process_retransmission_queue(ctx,
                                                                   &peer->reliable_queue,
                                                                   handle,
                                                                   current_time_ms);
    }

    return retransmit_count;
}

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

static xgl_error_t transport_cache_out_of_order_packet(xgl_transport_ctx_t* ctx,
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

static xgl_error_t transport_deliver_packet(xgl_transport_ctx_t* ctx,
                                            xgl_handle_t handle,
                                            const xgl_packet_t* packet,
                                            const uint8_t* data,
                                            size_t data_len) {
    if (ctx == NULL || packet == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_error_t err = XGL_OK;
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
            err = xgl_wire_ext_cursor_init(&cursor,
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

        err = xgl_fragment_process_ext(ctx->fragment_mgr,
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

static xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t* ctx,
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
                                 buffered->packet.session_id);
        transport_free_rx_buffered_packet(ctx, buffered);
        if (err != XGL_OK) {
            return err;
        }
        peer->rx_next_packet_number = packet_number + 1U;
    }
}

static void transport_mark_ack_range_windows(xgl_transport_ctx_t* ctx,
                                             xgl_transport_peer_state_t* peer,
                                             uint32_t largest_ack,
                                             const xgl_wire_ack_range_t* ranges,
                                             size_t range_count) {
    uint64_t next_high = largest_ack;

    for (size_t i = 0; i < range_count; ++i) {
        if (ranges[i].length == 0U) {
            continue;
        }

        uint64_t high = next_high;
        if (i > 0U) {
            uint64_t skip = (uint64_t)ranges[i].gap + 1U;
            if (high < skip) {
                break;
            }
            high -= skip;
        }

        uint64_t low = 0U;
        if (high + 1U > ranges[i].length) {
            low = high - (uint64_t)ranges[i].length + 1U;
        }

        for (uint64_t packet_number = high;; --packet_number) {
            (void)xgl_window_mark_ack_packet_number(&peer->tx_window,
                                                    (uint32_t)packet_number);
            if (xgl_window_is_in_window_packet_number(&ctx->window,
                                                      (uint32_t)packet_number)) {
                (void)xgl_window_mark_ack_packet_number(&ctx->window,
                                                        (uint32_t)packet_number);
            }

            if (packet_number == low) {
                break;
            }
        }

        next_high = low;
    }

    (void)xgl_window_advance_base_packet_number(&peer->tx_window);
    (void)xgl_window_advance_base_packet_number(&ctx->window);
}

static xgl_error_t transport_try_process_ack_range_ext(xgl_transport_ctx_t* ctx,
                                                       xgl_transport_peer_state_t* peer,
                                                       uint16_t source_id,
                                                       const uint8_t* data,
                                                       size_t data_len,
                                                       bool* handled) {
    if (ctx == NULL || peer == NULL || handled == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *handled = false;
    if (data == NULL || data_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&cursor, data, data_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type != XGL_WIRE_EXT_ACK_RANGE) {
            continue;
        }

        uint32_t largest_ack = 0;
        uint32_t ack_delay_us = 0;
        xgl_wire_ack_range_t ranges[64];
        size_t range_count = 0;
        err = xgl_wire_decode_ack_range_ext_value(ext.value,
                                                  ext.len,
                                                  &largest_ack,
                                                  &ack_delay_us,
                                                  ranges,
                                                  64U,
                                                  &range_count);
        (void)ack_delay_us;
        if (err != XGL_OK) {
            return err;
        }

        size_t removed = xgl_reliable_remove_ack_ranges(&peer->reliable_queue,
                                                        source_id,
                                                        largest_ack,
                                                        ranges,
                                                        range_count);
        if (removed == 0U) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        transport_mark_ack_range_windows(ctx, peer, largest_ack, ranges, range_count);
        *handled = true;
        return XGL_OK;
    }

    if (err == XGL_ERR_NOT_FOUND) {
        return XGL_OK;
    }

    return err;
}

static bool transport_sack_bit_is_set(const uint8_t* bitmap, size_t bit_index) {
    return (bitmap[bit_index / 8U] & (uint8_t)(1U << (bit_index % 8U))) != 0U;
}

static xgl_error_t transport_process_sack_value(xgl_transport_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                xgl_transport_peer_state_t* peer,
                                                uint16_t source_id,
                                                const uint8_t* value,
                                                size_t value_len,
                                                size_t* retransmitted) {
    if (ctx == NULL || peer == NULL || value == NULL || retransmitted == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint32_t base_packet = 0U;
    uint8_t bitmap[64] = {0};
    size_t bitmap_len = 0U;
    xgl_error_t err = xgl_wire_decode_sack_ext_value(value,
                                                     value_len,
                                                     &base_packet,
                                                     bitmap,
                                                     sizeof(bitmap),
                                                     &bitmap_len);
    if (err != XGL_OK) {
        return err;
    }
    if (bitmap_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t highest_set = 0U;
    bool has_set = false;
    for (size_t i = 0; i < bitmap_len * 8U; ++i) {
        if (transport_sack_bit_is_set(bitmap, i)) {
            highest_set = i;
            has_set = true;
        }
    }
    if (!has_set) {
        return XGL_ERR_INVALID_FRAME;
    }

    uint32_t now = xgl_time_ms();
    for (size_t i = 0; i <= highest_set; ++i) {
        uint32_t packet_number = base_packet + (uint32_t)i;
        bool received = transport_sack_bit_is_set(bitmap, i);
        if (received) {
            xgl_reliable_packet_t* rel_packet =
                xgl_reliable_find_packet_number(&peer->reliable_queue,
                                                packet_number,
                                                source_id);
            if (rel_packet != NULL) {
                (void)xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                                        packet_number,
                                                        source_id);
                (void)xgl_window_mark_ack_packet_number(&peer->tx_window,
                                                        packet_number);
                if (xgl_window_is_in_window_packet_number(&ctx->window,
                                                          packet_number)) {
                    (void)xgl_window_mark_ack_packet_number(&ctx->window,
                                                            packet_number);
                }
            }
            continue;
        }

        xgl_reliable_packet_t* missing =
            xgl_reliable_find_packet_number(&peer->reliable_queue,
                                            packet_number,
                                            source_id);
        if (missing != NULL) {
            err = transport_retransmit_reliable_packet(ctx, handle, missing, now);
            if (err != XGL_OK) {
                return err;
            }
            (*retransmitted)++;
        }
    }

    (void)xgl_window_advance_base_packet_number(&peer->tx_window);
    (void)xgl_window_advance_base_packet_number(&ctx->window);

    return XGL_OK;
}

static xgl_error_t transport_try_process_sack_ext(xgl_transport_ctx_t* ctx,
                                                  xgl_handle_t handle,
                                                  xgl_transport_peer_state_t* peer,
                                                  uint16_t source_id,
                                                  const uint8_t* data,
                                                  size_t data_len,
                                                  bool* handled) {
    if (ctx == NULL || peer == NULL || handled == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *handled = false;
    if (data == NULL || data_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&cursor, data, data_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type != XGL_WIRE_EXT_SACK) {
            continue;
        }

        size_t retransmitted = 0U;
        err = transport_process_sack_value(ctx,
                                           handle,
                                           peer,
                                           source_id,
                                           ext.value,
                                           ext.len,
                                           &retransmitted);
        if (err != XGL_OK) {
            return err;
        }

        if (retransmitted > 0U && ctx->tx_retries != NULL) {
            *ctx->tx_retries += retransmitted;
        }

        *handled = true;
        return XGL_OK;
    }

    if (err == XGL_ERR_NOT_FOUND) {
        return XGL_OK;
    }

    return err;
}

/*---------------------------------------------------------------------------*/
/* Transport Layer Initialization                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport layer context
 */
xgl_error_t xgl_transport_init(xgl_transport_ctx_t* ctx,
                               const xgl_transport_config_t* config) {
    xgl_error_t err;
    
    if (!ctx || !config) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (!config->stats) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
    ctx->local_id = config->local_id;
    ctx->max_retry_count = config->max_retry_count;
    ctx->default_timeout_ms = config->default_timeout_ms;
    ctx->enable_fragmentation = config->enable_fragmentation;
    ctx->max_frame_size = config->max_frame_size;
    ctx->route_table = config->route_table;
    ctx->next_session_id = (uint16_t)(config->local_id & XGL_SESSION_ID_MASK);
    if (ctx->next_session_id == 0U) {
        ctx->next_session_id = 1U;
    }
    ctx->lower_layer = config->lower_layer;
    ctx->rx_callback = config->rx_callback;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->stats = config->stats;
    ctx->tx_retries = config->tx_retries;
    ctx->allocator = config->allocator;
    
    /* Initialize RTT estimator */
    xgl_rtt_init(&ctx->rtt_est);
    
    /* Initialize sliding window */
    err = xgl_window_init_with_allocator(&ctx->window,
                                         config->window_size,
                                         config->allocator);
    if (err != XGL_OK) {
        return err;
    }
    
    /* Initialize reliable transmission queue */
    err = xgl_reliable_init(&ctx->reliable_queue, config->max_retry_count, config->allocator);
    if (err != XGL_OK) {
        xgl_window_destroy(&ctx->window);
        return err;
    }
    
    /* Initialize fragmentation manager if enabled */
    if (config->enable_fragmentation) {
        ctx->fragment_mgr = (xgl_fragment_manager_t*)transport_malloc(config->allocator, 
                                                                       sizeof(xgl_fragment_manager_t));
        if (!ctx->fragment_mgr) {
            xgl_reliable_destroy(&ctx->reliable_queue);
            xgl_window_destroy(&ctx->window);
            return XGL_ERR_NO_MEMORY;
        }
        
        err = xgl_fragment_init(ctx->fragment_mgr, 8, XGL_FRAGMENT_TIMEOUT_MS, config->allocator);
        if (err != XGL_OK) {
            transport_free(config->allocator, ctx->fragment_mgr);
            ctx->fragment_mgr = NULL;
            xgl_reliable_destroy(&ctx->reliable_queue);
            xgl_window_destroy(&ctx->window);
            return err;
        }
    }
    
    return XGL_OK;
}

/**
 * \brief           Destroy transport layer context
 */
void xgl_transport_destroy(xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return;
    }
    
    /* Destroy fragmentation manager if allocated */
    if (ctx->fragment_mgr) {
        xgl_fragment_destroy(ctx->fragment_mgr);
        transport_free(ctx->allocator, ctx->fragment_mgr);
        ctx->fragment_mgr = NULL;
    }
    
    /* Destroy reliable transmission queue */
    xgl_reliable_destroy(&ctx->reliable_queue);
    
    /* Destroy sliding window */
    xgl_window_destroy(&ctx->window);

    transport_destroy_peers(ctx);
    
    /* Clear context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
}

/*---------------------------------------------------------------------------*/
/* Transport Layer Send                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send data through transport layer
 */
xgl_error_t xgl_transport_send(xgl_transport_ctx_t* ctx,
                               xgl_handle_t handle,
                               const xgl_tx_data_t* tx_data) {
    xgl_error_t err;
    
    (void)handle;  /* Unused parameter */
    
    if (!ctx || !tx_data || !tx_data->data) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (tx_data->data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Check if lower layer is connected */
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        if (ctx->error_callback) {
            ctx->error_callback(handle, XGL_ERR_INVALID_PARAM,
                              "Transport layer not connected to network layer",
                              ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_PARAM;
    }
    
    xgl_transport_peer_state_t* peer = NULL;
    bool has_tx_scope =
        (tx_data->connection_id != 0U || tx_data->session_epoch != 0U);
    if (tx_data->reliable) {
        peer = has_tx_scope ?
            transport_get_or_create_peer_scope(ctx,
                                               tx_data->target_id,
                                               tx_data->connection_id,
                                               tx_data->session_epoch) :
            transport_get_or_create_peer(ctx, tx_data->target_id);
        if (peer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }

        if (!xgl_window_can_send_packet_number(&peer->tx_window)) {
            return XGL_ERR_WINDOW_FULL;
        }

        if (!peer->hello_sent) {
            err = transport_send_control(ctx,
                                         handle,
                                         tx_data->target_id,
                                         XGL_TRANSPORT_CONTROL_HELLO,
                                         0,
                                         peer->session_id,
                                         peer->connection_id,
                                         peer->session_epoch);
            if (err != XGL_OK) {
                return err;
            }
            peer->hello_sent = true;
        }
    }
    
    uint16_t effective_max_frame_size = ctx->max_frame_size;
    if (ctx->route_table != NULL) {
        xgl_route_item_t* route = xgl_route_table_lookup(ctx->route_table,
                                                         tx_data->target_id);
        if (route != NULL && route->max_frame_size < effective_max_frame_size) {
            effective_max_frame_size = route->max_frame_size;
        }
    }

    /* Validate frame size before calculating payload size */
    size_t min_frame_size = XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE;
    if (effective_max_frame_size < min_frame_size) {
        if (ctx->stats) {
            ctx->stats->tx_errors++;
        }
        if (ctx->error_callback) {
            ctx->error_callback(handle, XGL_ERR_INVALID_PARAM,
                              "max_frame_size too small for headers",
                              ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Determine if fragmentation is needed */
    size_t max_payload_size = (size_t)effective_max_frame_size -
                              XGL_FRAME_HEADER_SIZE -
                              XGL_CRC16_SIZE;
    bool needs_fragmentation = (tx_data->data_len > max_payload_size) && ctx->enable_fragmentation;

    if (tx_data->data_len > max_payload_size && !ctx->enable_fragmentation) {
        if (ctx->stats) {
            ctx->stats->tx_errors++;
        }
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    if (needs_fragmentation) {
        if (!ctx->fragment_mgr) {
            return XGL_ERR_INVALID_PARAM;
        }

        const size_t fragment_ext_len = XGL_WIRE_EXT_HEADER_SIZE + 12U;
        if (max_payload_size <= fragment_ext_len) {
            if (ctx->stats) {
                ctx->stats->tx_errors++;
            }
            return XGL_ERR_BUFFER_TOO_SMALL;
        }

        size_t fragment_payload_max = max_payload_size - fragment_ext_len;
        uint32_t message_id = ctx->fragment_mgr->next_message_id++;
        size_t fragment_count =
            (tx_data->data_len + fragment_payload_max - 1U) / fragment_payload_max;

        for (size_t i = 0; i < fragment_count; i++) {
            size_t fragment_offset = i * fragment_payload_max;
            size_t remaining = tx_data->data_len - fragment_offset;
            size_t fragment_payload_len = (remaining < fragment_payload_max) ?
                                          remaining : fragment_payload_max;

            if (tx_data->reliable && peer != NULL &&
                !xgl_window_can_send_packet_number(&peer->tx_window)) {
                return XGL_ERR_WINDOW_FULL;
            }

            uint32_t packet_number = 0;
            if (tx_data->reliable && peer != NULL) {
                packet_number = transport_allocate_packet_number(ctx, peer);
            }
            
            /* Get timeout - use custom timeout if provided, otherwise use RTT estimate or default */
            int32_t timeout_ms;
            if (tx_data->timeout_ms > 0) {
                timeout_ms = (int32_t)tx_data->timeout_ms;
            } else {
                timeout_ms = (peer != NULL) ? xgl_rtt_get_rto(&peer->rtt_est) :
                                              xgl_rtt_get_rto(&ctx->rtt_est);
                if (timeout_ms == 0) {
                    timeout_ms = (int32_t)ctx->default_timeout_ms;
                }
            }
            
            /* Note: Route lookup is now handled by network layer */
            /* The PHY will be determined when the packet reaches network layer */
            xgl_phy_ops_t* phy = NULL;  /* Will be set by network layer */

            uint8_t fragment_ext_value[12] = {0};
            size_t fragment_ext_value_len = 0;
            err = xgl_wire_encode_fragment_ext_value(fragment_ext_value,
                                                     sizeof(fragment_ext_value),
                                                     message_id,
                                                     (uint32_t)fragment_offset,
                                                     (uint32_t)tx_data->data_len,
                                                     &fragment_ext_value_len);
            if (err != XGL_OK) {
                return err;
            }

            uint8_t fragment_ext[XGL_WIRE_EXT_HEADER_SIZE + 12U] = {0};
            size_t encoded_ext_len = 0;
            err = xgl_wire_encode_ext(fragment_ext,
                                      sizeof(fragment_ext),
                                      XGL_WIRE_EXT_FRAGMENT,
                                      fragment_ext_value,
                                      fragment_ext_value_len,
                                      &encoded_ext_len);
            if (err != XGL_OK) {
                return err;
            }
            
            /* Add to reliable queue if needed */
            if (tx_data->reliable) {
                err = xgl_reliable_add_packet_number(&peer->reliable_queue,
                                                     &tx_data->data[fragment_offset],
                                                     fragment_payload_len,
                                                     ctx->local_id, tx_data->target_id,
                                                     packet_number, tx_data->data_type,
                                                     tx_data->priority, timeout_ms, phy);
                if (err != XGL_OK) {
                    return err;
                }

                xgl_reliable_packet_t* rel_packet =
                    xgl_reliable_find_packet_number(&peer->reliable_queue,
                                                    packet_number,
                                                    tx_data->target_id);
                if (rel_packet != NULL) {
                    rel_packet->session_id = peer->session_id;
                    rel_packet->connection_id = tx_data->connection_id;
                    rel_packet->session_epoch = tx_data->session_epoch;
                    rel_packet->send_timestamp = xgl_time_ms();
                    rel_packet->packet_type = XGL_PACKET_TYPE_DATA;
                    rel_packet->flags = XGL_WIRE_FLAG_FRAGMENTED |
                                        XGL_WIRE_FLAG_HAS_EXTENSIONS;
                    rel_packet->fragment = true;
                    err = xgl_reliable_set_packet_extensions(&peer->reliable_queue,
                                                             rel_packet,
                                                             fragment_ext,
                                                             encoded_ext_len);
                    if (err != XGL_OK) {
                        (void)xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                                                packet_number,
                                                                tx_data->target_id);
                        return err;
                    }
                }
            }
            
            /* Send fragment through network layer via interface */
            xgl_packet_data_t packet_data = {
                .ref_count = 1,
                .data_len = fragment_payload_len,
                .data = &tx_data->data[fragment_offset],
                .owned_data = NULL
            };
            
            xgl_packet_t packet = {
                .source_id = ctx->local_id,
                .target_id = tx_data->target_id,
                .packet_number = packet_number,
                .session_id = (peer != NULL) ? peer->session_id : 0,
                .connection_id = tx_data->connection_id,
                .session_epoch = tx_data->session_epoch,
                .data_type = tx_data->data_type,
                .reliable = tx_data->reliable,
                .fragment = true,  /* Mark as fragment */
                .priority = tx_data->priority,
                .data = &packet_data,
                .extensions = fragment_ext,
                .extensions_len = encoded_ext_len,
                .phy = NULL  /* Will be set by network layer */
            };
            
            /* Send packet through network layer via interface */
            if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
                /* Update error statistics */
                if (ctx->stats) {
                    ctx->stats->tx_errors++;
                }
                return XGL_ERR_INVALID_PARAM;
            }
            
            err = xgl_layer_send(ctx->lower_layer, handle, &packet);
            
            if (err != XGL_OK) {
                /* Update error statistics */
                if (ctx->stats) {
                    ctx->stats->tx_errors++;
                }
                return err;
            }
        }
        
    } else {
        /* Send without fragmentation */
        
        /* Allocate packet number */
        uint32_t packet_number = 0;
        if (tx_data->reliable && peer != NULL) {
            packet_number = transport_allocate_packet_number(ctx, peer);
        }
        
        /* Get timeout - use custom timeout if provided, otherwise use RTT estimate or default */
        int32_t timeout_ms;
        if (tx_data->timeout_ms > 0) {
            timeout_ms = (int32_t)tx_data->timeout_ms;
        } else {
            timeout_ms = (peer != NULL) ? xgl_rtt_get_rto(&peer->rtt_est) :
                                          xgl_rtt_get_rto(&ctx->rtt_est);
                if (timeout_ms == 0) {
                    timeout_ms = (int32_t)ctx->default_timeout_ms;
                }
        }
        
        /* Create packet data structure for network layer */
        xgl_packet_data_t packet_data = {
            .ref_count = 1,
            .data_len = tx_data->data_len,
            .data = tx_data->data,
            .owned_data = NULL
        };
        
        xgl_packet_t packet = {
            .source_id = ctx->local_id,
            .target_id = tx_data->target_id,
            .session_id = (peer != NULL) ? peer->session_id : 0,
            .connection_id = tx_data->connection_id,
            .packet_number = packet_number,
            .session_epoch = tx_data->session_epoch,
            .data_type = tx_data->data_type,
            .reliable = tx_data->reliable,
            .fragment = false,  /* Not a fragment */
            .priority = tx_data->priority,
            .data = &packet_data,
            .phy = NULL  /* Will be set by network layer */
        };
        
        /* Send packet through network layer via interface */
        if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
            /* Update error statistics */
            if (ctx->stats) {
                ctx->stats->tx_errors++;
            }
            return XGL_ERR_INVALID_PARAM;
        }
        
        err = xgl_layer_send(ctx->lower_layer, handle, &packet);
        
        if (err != XGL_OK) {
            /* Update error statistics */
            if (ctx->stats) {
                ctx->stats->tx_errors++;
            }
            return err;
        }
        
        /* Add to reliable queue if needed */
        if (tx_data->reliable) {
            err = xgl_reliable_add_packet_number(&peer->reliable_queue,
                                                 tx_data->data, tx_data->data_len,
                                                 ctx->local_id, tx_data->target_id,
                                                 packet_number, tx_data->data_type,
                                                 tx_data->priority, timeout_ms, packet.phy);
            if (err != XGL_OK) {
                return err;
            }

            xgl_reliable_packet_t* rel_packet =
                xgl_reliable_find_packet_number(&peer->reliable_queue,
                                                packet_number,
                                                tx_data->target_id);
            if (rel_packet != NULL) {
                rel_packet->session_id = peer->session_id;
                rel_packet->connection_id = tx_data->connection_id;
                rel_packet->session_epoch = tx_data->session_epoch;
                rel_packet->send_timestamp = xgl_time_ms();
                rel_packet->packet_type = XGL_PACKET_TYPE_DATA;
                rel_packet->fragment = false;
            }
        }
    }
    
    /* Update statistics */
    ctx->stats->tx_packets++;
    ctx->stats->tx_bytes += tx_data->data_len;
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Transport Layer Receive                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Receive and process packet from network layer
 */
xgl_error_t xgl_transport_receive(xgl_transport_ctx_t* ctx,
                                  xgl_handle_t handle,
                                  const xgl_packet_t* packet) {
    xgl_error_t err;
    
    if (!ctx || !packet) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Extract packet fields */
    uint16_t source_id = packet->source_id;
    uint8_t data_type = packet->data_type;
    uint8_t reliable = packet->reliable;
    bool has_connection_scope =
        (packet->connection_id != 0U || packet->session_epoch != 0U);

    if (data_type == XGL_TRANSPORT_CONTROL_HELLO ||
        data_type == XGL_TRANSPORT_CONTROL_RESET) {
        return transport_process_control_packet(ctx, packet);
    }
    
    /* Extract payload data from packet */
    const uint8_t* data = NULL;
    size_t data_len = 0;
    if (packet->data != NULL) {
        data = packet->data->data;
        data_len = packet->data->data_len;
    }
    
    /* Check if this is an ACK packet */
    if (reliable == XGL_RELIABILITY_ACK_ONLY) {
        xgl_transport_peer_state_t* peer = has_connection_scope ?
            transport_find_peer_scope(ctx,
                                      source_id,
                                      packet->connection_id,
                                      packet->session_epoch) :
            transport_find_peer(ctx, source_id);
        if (peer == NULL && has_connection_scope) {
            peer = transport_find_peer(ctx, source_id);
        }
        if (peer == NULL) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        if (packet->session_id != 0U && packet->session_id != peer->session_id) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        if ((packet->flags & XGL_WIRE_FLAG_HAS_EXTENSIONS) == 0U) {
            return XGL_ERR_INVALID_FRAME;
        }

        bool handled_ack_range = false;
        err = transport_try_process_ack_range_ext(ctx,
                                                  peer,
                                                  source_id,
                                                  data,
                                                  data_len,
                                                  &handled_ack_range);
        if (err != XGL_OK) {
            return err;
        }
        if (handled_ack_range) {
            return XGL_OK;
        }

        bool handled_sack = false;
        err = transport_try_process_sack_ext(ctx,
                                             handle,
                                             peer,
                                             source_id,
                                             data,
                                             data_len,
                                             &handled_sack);
        if (err != XGL_OK) {
            return err;
        }
        if (handled_sack) {
            return XGL_OK;
        }

        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Validate data pointer for non-ACK packets */
    if (data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_transport_peer_state_t* rx_peer = NULL;
    if (packet->session_id != 0U) {
        rx_peer = has_connection_scope ?
            transport_find_peer_scope(ctx,
                                      source_id,
                                      packet->connection_id,
                                      packet->session_epoch) :
            transport_find_peer(ctx, source_id);
        if (rx_peer == NULL) {
            rx_peer = has_connection_scope ?
                transport_get_or_create_peer_scope(ctx,
                                                   source_id,
                                                   packet->connection_id,
                                                   packet->session_epoch) :
                transport_get_or_create_peer(ctx, source_id);
            if (rx_peer == NULL) {
                return XGL_ERR_NO_MEMORY;
            }
            transport_reset_peer_state(ctx,
                                       rx_peer,
                                       packet->session_id,
                                       packet->connection_id,
                                       packet->session_epoch);
        } else if (packet->session_id != rx_peer->session_id) {
            return XGL_ERR_SEQUENCE_ERROR;
        }
    }
    
    /* Check for duplicate reliable packet */
    if (reliable == XGL_RELIABILITY_ACK_ELICITING) {
        if (rx_peer == NULL) {
            rx_peer = has_connection_scope ?
                transport_get_or_create_peer_scope(ctx,
                                                   source_id,
                                                   packet->connection_id,
                                                   packet->session_epoch) :
                transport_get_or_create_peer(ctx, source_id);
            if (rx_peer == NULL) {
                return XGL_ERR_NO_MEMORY;
            }
        }

        uint32_t packet_number = transport_receive_packet_number(packet);
        if (!rx_peer->rx_has_packet_number_state) {
            rx_peer->rx_next_packet_number = 0U;
            rx_peer->rx_has_packet_number_state = true;
        }

        if (packet_number < rx_peer->rx_next_packet_number) {
            /* Sender may have missed our previous ACK. */
            transport_send_ack(ctx, handle, packet_number, source_id, packet->session_id);
            return XGL_OK;
        }

        if (packet_number > rx_peer->rx_next_packet_number) {
            uint32_t expected_packet_number = rx_peer->rx_next_packet_number;
            err = transport_cache_out_of_order_packet(ctx, rx_peer, packet, packet_number);
            (void)transport_send_control(ctx,
                                         handle,
                                         source_id,
                                         XGL_TRANSPORT_CONTROL_NACK,
                                         expected_packet_number,
                                         packet->session_id,
                                         packet->connection_id,
                                         packet->session_epoch);
            if (err != XGL_OK && ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            return err;
        }

        transport_send_ack(ctx, handle, packet_number, source_id, packet->session_id);
    }

    err = transport_deliver_packet(ctx, handle, packet, data, data_len);
    if (err != XGL_OK) {
        return err;
    }

    if (reliable == XGL_RELIABILITY_ACK_ELICITING && rx_peer != NULL) {
        uint32_t packet_number = transport_receive_packet_number(packet);
        rx_peer->rx_next_packet_number = packet_number + 1U;
        return transport_drain_rx_buffered(ctx, handle, rx_peer);
    }

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Transport Layer Periodic Processing                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Periodic transport layer processing
 */
xgl_error_t xgl_transport_run(xgl_transport_ctx_t* ctx,
                              xgl_handle_t handle,
                              uint32_t current_time_ms) {
    if (!ctx) {
        return XGL_ERR_NULL_POINTER;
    }
    
    uint32_t retransmit_count = transport_process_retransmissions(ctx, handle, current_time_ms);
    
    /* Update retransmission statistics */
    if (retransmit_count > 0 && ctx->tx_retries != NULL) {
        (*ctx->tx_retries) += retransmit_count;
    }
    
    /* Process fragment reassembly timeouts */
    if (ctx->fragment_mgr) {
        uint32_t timeout_count = xgl_fragment_process_timeouts(ctx->fragment_mgr,
                                                                current_time_ms);
        if (timeout_count > 0) {
            /* Report error */
            if (ctx->error_callback) {
                ctx->error_callback(handle, XGL_ERR_TIMEOUT,
                                  "Fragment reassembly timeout",
                                  ctx->callback_user_data);
            }
            
            /* Update statistics */
            ctx->stats->rx_dropped += timeout_count;
        }
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Transport Layer Utility Functions                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get next packet number for target
 */
uint32_t xgl_transport_get_next_packet_number(xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return 0;
    }
    return xgl_window_get_next_packet_number(&ctx->window);
}

/**
 * \brief           Check if transport layer can send
 */
bool xgl_transport_can_send(const xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return false;
    }
    return xgl_window_can_send_packet_number(&ctx->window);
}

/**
 * \brief           Report error through error callback
 */
void xgl_transport_report_error(xgl_transport_ctx_t* ctx,
                                xgl_handle_t handle,
                                xgl_error_t error,
                                const char* message) {
    if (!ctx) {
        return;
    }
    
    if (ctx->error_callback) {
        ctx->error_callback(handle, error, message, ctx->callback_user_data);
    }
}


/*---------------------------------------------------------------------------*/
/* Layer Interface Implementation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Transport layer send implementation (not used - app calls transport_send directly)
 * \details         This function would be called by application layer if using interface pattern
 */
static xgl_error_t transport_send_impl(void* ctx,
                                      xgl_handle_t handle,
                                      void* data) {
    (void)ctx;
    (void)handle;
    (void)data;
    /* Application calls xgl_transport_send directly, not through interface */
    return XGL_ERR_INVALID_PARAM;
}

/**
 * \brief           Transport layer receive implementation (called by lower layers)
 * \details         This function is called by network layer to deliver packets
 */
static xgl_error_t transport_receive_impl(void* ctx,
                                         xgl_handle_t handle,
                                         void* data) {
    xgl_transport_ctx_t* trans_ctx = (xgl_transport_ctx_t*)ctx;
    xgl_packet_t* packet = (xgl_packet_t*)data;
    
    if (trans_ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Forward to transport receive function */
    return xgl_transport_receive(trans_ctx, handle, packet);
}

/**
 * \brief           Transport layer error reporting implementation
 * \details         This function is called to report errors to application
 */
static xgl_error_t transport_report_error_impl(void* ctx,
                                              xgl_handle_t handle,
                                              void* data) {
    xgl_transport_ctx_t* trans_ctx = (xgl_transport_ctx_t*)ctx;
    xgl_layer_error_info_t* error_info = (xgl_layer_error_info_t*)data;
    
    if (trans_ctx == NULL || error_info == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Forward error to callback if available */
    if (trans_ctx->error_callback != NULL) {
        trans_ctx->error_callback(handle, error_info->error,
                                 error_info->message,
                                 trans_ctx->callback_user_data);
    }
    
    return XGL_OK;
}

/**
 * \brief           Get transport layer interface
 * \details         Returns the layer interface for this transport instance
 * \param[in]       ctx: Transport layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_transport_get_interface(xgl_transport_ctx_t* ctx,
                                       xgl_layer_interface_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    xgl_layer_interface_init(iface,
                            ctx,
                            transport_send_impl,
                            transport_receive_impl,
                            transport_report_error_impl);
    
    return XGL_OK;
}
