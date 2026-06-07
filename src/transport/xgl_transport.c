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

static xgl_transport_peer_state_t* transport_get_or_create_peer(xgl_transport_ctx_t* ctx,
                                                                uint16_t peer_id) {
    xgl_transport_peer_state_t* peer = transport_find_peer(ctx, peer_id);
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
    peer->session_id = (uint16_t)(ctx->next_session_id & XGL_ATTR_SESSION_MASK);
    if (peer->session_id == 0U) {
        peer->session_id = 1U;
    }
    ctx->next_session_id = (uint16_t)((peer->session_id + 1U) & XGL_ATTR_SESSION_MASK);
    if (ctx->next_session_id == 0U) {
        ctx->next_session_id = 1U;
    }
    xgl_rtt_init(&peer->rtt_est);

    xgl_error_t err = xgl_window_init(&peer->tx_window, ctx->window.window_size);
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

static void transport_destroy_peers(xgl_transport_ctx_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    xgl_transport_peer_state_t* peer = ctx->peers;
    while (peer != NULL) {
        xgl_transport_peer_state_t* next = peer->next;
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

    peer->tx_window.next_seq_num =
        (uint8_t)(xgl_window_get_next_packet_number(&peer->tx_window) & 0xFFU);
    if (ctx != NULL) {
        ctx->window.next_seq_num =
            (uint8_t)(xgl_window_get_next_packet_number(&ctx->window) & 0xFFU);
    }

    return packet_number;
}

static void transport_reset_peer_state(xgl_transport_ctx_t* ctx,
                                       xgl_transport_peer_state_t* peer,
                                       uint16_t session_id) {
    if (ctx == NULL || peer == NULL) {
        return;
    }

    peer->session_id = (uint16_t)(session_id & XGL_ATTR_SESSION_MASK);
    peer->hello_sent = false;
    peer->session_established = true;
    xgl_reliable_clear(&peer->reliable_queue);
    xgl_window_reset(&peer->tx_window);
    xgl_window_reset(&ctx->window);
    xgl_ack_reset(&ctx->ack_handler);
    if (ctx->fragment_mgr != NULL) {
        xgl_fragment_clear_reassembly(ctx->fragment_mgr);
    }
    xgl_rtt_init(&peer->rtt_est);
    peer->last_active_ms = xgl_time_ms();
}

static xgl_error_t transport_send_control(xgl_transport_ctx_t* ctx,
                                          xgl_handle_t handle,
                                          uint16_t target_id,
                                          uint8_t control_type,
                                          uint8_t ack_num,
                                          uint16_t session_id) {
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = NULL,
        .owned_data = NULL
    };

    xgl_packet_t packet = {
        .source_id = ctx->local_id,
        .target_id = target_id,
        .data_type = control_type,
        .seq_num = 0,
        .ack_num = ack_num,
        .session_id = session_id,
        .reliable = (control_type == XGL_TRANSPORT_CONTROL_NACK ||
                     control_type == XGL_TRANSPORT_CONTROL_SACK) ?
                    XGL_ATTR_RELIABLE_ACK : XGL_ATTR_RELIABLE_NONE,
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

    uint16_t session_id = (uint16_t)(packet->session_id & XGL_ATTR_SESSION_MASK);
    if (session_id == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_transport_peer_state_t* peer =
        transport_get_or_create_peer(ctx, packet->source_id);
    if (peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    if (packet->data_type == XGL_TRANSPORT_CONTROL_RESET ||
        peer->session_id != session_id) {
        transport_reset_peer_state(ctx, peer, session_id);
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
 * \param[in]       seq_num: Sequence number to acknowledge
 * \param[in]       source_id: Source node ID
 * \return          XGL_OK on success, error code otherwise
 */
static xgl_error_t transport_send_ack(xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     uint8_t seq_num,
                                     uint16_t source_id,
                                     uint16_t session_id) {
    xgl_packet_data_t ack_packet_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = NULL,
        .owned_data = NULL
    };
    
    xgl_packet_t ack_packet = {
            .source_id = ctx->local_id,
        .target_id = source_id,
        .data_type = 0,
        .seq_num = 0,
        .ack_num = seq_num,
        .session_id = session_id,
            .reliable = XGL_ATTR_RELIABLE_ACK,
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

        xgl_packet_data_t packet_data = {
            .ref_count = 1,
            .data_len = rel_packet->data_len,
            .data = rel_packet->data,
            .owned_data = NULL
        };

        xgl_packet_t packet = {
            .source_id = rel_packet->source_id,
            .target_id = rel_packet->target_id,
            .packet_number = rel_packet->packet_number,
            .seq_num = rel_packet->seq_num,
            .ack_num = 0,
            .session_id = rel_packet->session_id,
            .data_type = rel_packet->data_type,
            .reliable = true,
            .fragment = false,
            .priority = rel_packet->priority,
            .data = &packet_data,
            .phy = rel_packet->phy
        };

        xgl_error_t err = xgl_layer_send(ctx->lower_layer, handle, &packet);
        if (err == XGL_OK) {
            rel_packet->retry_count++;
            rel_packet->timeout_ms = xgl_reliable_calc_backoff(
                rel_packet->initial_timeout_ms,
                rel_packet->retry_count
            );
            rel_packet->send_timestamp = current_time_ms;
            retransmit_count++;
        } else if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
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
    ctx->next_session_id = (uint16_t)(config->local_id & XGL_ATTR_SESSION_MASK);
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
    err = xgl_window_init(&ctx->window, config->window_size);
    if (err != XGL_OK) {
        return err;
    }
    
    /* Initialize reliable transmission queue */
    err = xgl_reliable_init(&ctx->reliable_queue, config->max_retry_count, config->allocator);
    if (err != XGL_OK) {
        xgl_window_destroy(&ctx->window);
        return err;
    }
    
    /* Initialize ACK handler */
    err = xgl_ack_init(&ctx->ack_handler, config->allocator);
    if (err != XGL_OK) {
        xgl_reliable_destroy(&ctx->reliable_queue);
        xgl_window_destroy(&ctx->window);
        return err;
    }
    
    /* Initialize fragmentation manager if enabled */
    if (config->enable_fragmentation) {
        ctx->fragment_mgr = (xgl_fragment_manager_t*)transport_malloc(config->allocator, 
                                                                       sizeof(xgl_fragment_manager_t));
        if (!ctx->fragment_mgr) {
            xgl_ack_destroy(&ctx->ack_handler);
            xgl_reliable_destroy(&ctx->reliable_queue);
            xgl_window_destroy(&ctx->window);
            return XGL_ERR_NO_MEMORY;
        }
        
        err = xgl_fragment_init(ctx->fragment_mgr, 8, XGL_FRAGMENT_TIMEOUT_MS, config->allocator);
        if (err != XGL_OK) {
            transport_free(config->allocator, ctx->fragment_mgr);
            ctx->fragment_mgr = NULL;
            xgl_ack_destroy(&ctx->ack_handler);
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
    
    /* Destroy ACK handler */
    xgl_ack_destroy(&ctx->ack_handler);
    
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
    if (tx_data->reliable) {
        peer = transport_get_or_create_peer(ctx, tx_data->target_id);
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
                                         peer->session_id);
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
        /* Fragment and send */
        if (!ctx->fragment_mgr) {
            return XGL_ERR_INVALID_PARAM;
        }
        
        /* Allocate fragment arrays */
        size_t max_fragments = (tx_data->data_len + max_payload_size - 1) / max_payload_size;
        
        uint8_t** fragments = (uint8_t**)transport_malloc(ctx->allocator, 
                                                           max_fragments * sizeof(uint8_t*));
        size_t* fragment_lens = (size_t*)transport_malloc(ctx->allocator, 
                                                          max_fragments * sizeof(size_t));
        
        if (!fragments || !fragment_lens) {
            if (fragments) transport_free(ctx->allocator, fragments);
            if (fragment_lens) transport_free(ctx->allocator, fragment_lens);
            return XGL_ERR_NO_MEMORY;
        }
        
        /* Fragment data */
        size_t fragment_count = max_fragments;
        uint8_t fragment_id;
        err = xgl_fragment_data(ctx->fragment_mgr, tx_data->data, tx_data->data_len,
                               max_payload_size, fragments, fragment_lens,
                               &fragment_count, &fragment_id);
        
        if (err != XGL_OK) {
            transport_free(ctx->allocator, fragments);
            transport_free(ctx->allocator, fragment_lens);
            return err;
        }
        
        /* Send each fragment */
        for (size_t i = 0; i < fragment_count; i++) {
            /* Get sequence number */
            if (tx_data->reliable && peer != NULL &&
                !xgl_window_can_send_packet_number(&peer->tx_window)) {
                xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
                transport_free(ctx->allocator, fragments);
                transport_free(ctx->allocator, fragment_lens);
                return XGL_ERR_WINDOW_FULL;
            }

            uint32_t packet_number = 0;
            uint8_t seq_num = 0;
            if (tx_data->reliable && peer != NULL) {
                packet_number = transport_allocate_packet_number(ctx, peer);
                seq_num = (uint8_t)(packet_number & 0xFFU);
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
            
            /* Add to reliable queue if needed */
            if (tx_data->reliable) {
                err = xgl_reliable_add_packet_number(&peer->reliable_queue,
                                                     fragments[i], fragment_lens[i],
                                                     ctx->local_id, tx_data->target_id,
                                                     packet_number, tx_data->data_type,
                                                     tx_data->priority, timeout_ms, phy);
                if (err != XGL_OK) {
                    xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
                    transport_free(ctx->allocator, fragments);
                    transport_free(ctx->allocator, fragment_lens);
                    return err;
                }

                xgl_reliable_packet_t* rel_packet =
                    xgl_reliable_find_packet_number(&peer->reliable_queue,
                                                    packet_number,
                                                    tx_data->target_id);
                if (rel_packet != NULL) {
                    rel_packet->session_id = peer->session_id;
                    rel_packet->send_timestamp = xgl_time_ms();
                }
            }
            
            /* Send fragment through network layer via interface */
                xgl_packet_data_t packet_data = {
                    .ref_count = 1,
                    .data_len = fragment_lens[i],
                    .data = fragments[i],
                    .owned_data = NULL
                };
            
            xgl_packet_t packet = {
                .source_id = ctx->local_id,
                .target_id = tx_data->target_id,
                .data_type = tx_data->data_type,
                .packet_number = packet_number,
                .seq_num = seq_num,
                .ack_num = 0,  /* ACK number is 0 for data packets */
                .session_id = (peer != NULL) ? peer->session_id : 0,
                .reliable = tx_data->reliable,
                .fragment = true,  /* Mark as fragment */
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
                xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
                transport_free(ctx->allocator, fragments);
                transport_free(ctx->allocator, fragment_lens);
                return XGL_ERR_INVALID_PARAM;
            }
            
            err = xgl_layer_send(ctx->lower_layer, handle, &packet);
            
            if (err != XGL_OK) {
                /* Update error statistics */
                if (ctx->stats) {
                    ctx->stats->tx_errors++;
                }
                xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
                transport_free(ctx->allocator, fragments);
                transport_free(ctx->allocator, fragment_lens);
                return err;
            }
        }
        
        /* Clean up */
        xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
        transport_free(ctx->allocator, fragments);
        transport_free(ctx->allocator, fragment_lens);
        
    } else {
        /* Send without fragmentation */
        
        /* Get sequence number */
        uint32_t packet_number = 0;
        uint8_t seq_num = 0;
        if (tx_data->reliable && peer != NULL) {
            packet_number = transport_allocate_packet_number(ctx, peer);
            seq_num = (uint8_t)(packet_number & 0xFFU);
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
            .data_type = tx_data->data_type,
            .packet_number = packet_number,
            .seq_num = seq_num,
            .ack_num = 0,  /* ACK number is 0 for data packets */
            .session_id = (peer != NULL) ? peer->session_id : 0,
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
                rel_packet->send_timestamp = xgl_time_ms();
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
    uint8_t seq_num = packet->seq_num;
    uint8_t ack_num = packet->ack_num;
    uint8_t data_type = packet->data_type;
    uint8_t reliable = packet->reliable;

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
    if (reliable == XGL_ATTR_RELIABLE_ACK) {
        xgl_transport_peer_state_t* peer = transport_find_peer(ctx, source_id);
        if (peer == NULL) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        if (packet->session_id != 0U && packet->session_id != peer->session_id) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        if ((packet->flags & XGL_WIRE_FLAG_HAS_EXTENSIONS) != 0U) {
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
        }

        /* Process ACK */
        bool is_valid = false;
        err = xgl_ack_process(&ctx->ack_handler, ack_num, source_id, &is_valid);
        if (err != XGL_OK || !is_valid) {
            return err;
        }

        xgl_reliable_packet_t* rel_packet =
            xgl_reliable_find_packet_number(&peer->reliable_queue,
                                            (uint32_t)ack_num,
                                            source_id);
        if (rel_packet == NULL) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        uint32_t measured_rtt_ms = 0;
        bool has_rtt_sample = false;
        if (rel_packet->send_timestamp != 0U) {
            measured_rtt_ms = xgl_time_ms() - rel_packet->send_timestamp;
            has_rtt_sample = true;
        }
        uint32_t acked_packet_number = rel_packet->packet_number;
        
        /* Remove packet from reliable queue */
        err = xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                                acked_packet_number,
                                                source_id);
        if (err == XGL_OK) {
            /* Update RTT estimate */
            if (has_rtt_sample) {
                xgl_rtt_update(&peer->rtt_est, (int32_t)measured_rtt_ms);
            }
            
            /* Advance peer-specific sliding window */
            err = xgl_window_mark_ack_packet_number(&peer->tx_window,
                                                    acked_packet_number);
            if (err != XGL_OK) {
                return err;
            }
            xgl_window_advance_base_packet_number(&peer->tx_window);

            if (xgl_window_is_in_window_packet_number(&ctx->window,
                                                      acked_packet_number)) {
                (void)xgl_window_mark_ack_packet_number(&ctx->window,
                                                        acked_packet_number);
                (void)xgl_window_advance_base_packet_number(&ctx->window);
            }
        } else {
            return err;
        }
        
        return XGL_OK;
    }
    
    /* Validate data pointer for non-ACK packets */
    if (data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (packet->session_id != 0U) {
        xgl_transport_peer_state_t* peer = transport_find_peer(ctx, source_id);
        if (peer == NULL) {
        peer = transport_get_or_create_peer(ctx, source_id);
            if (peer == NULL) {
                return XGL_ERR_NO_MEMORY;
            }
            transport_reset_peer_state(ctx, peer, packet->session_id);
        } else if (packet->session_id != peer->session_id) {
            return XGL_ERR_SEQUENCE_ERROR;
        }
    }
    
    /* Check for duplicate reliable packet */
    if (reliable == XGL_ATTR_RELIABLE_TX) {
        if (xgl_ack_is_duplicate_from(&ctx->ack_handler, source_id, seq_num)) {
            /* Sender may have missed our previous ACK. */
            transport_send_ack(ctx, handle, seq_num, source_id, packet->session_id);
            return XGL_OK;
        }

        if (xgl_ack_is_out_of_order_from(&ctx->ack_handler, source_id, seq_num)) {
            uint8_t expected_seq = xgl_ack_get_expected_from(&ctx->ack_handler, source_id);
            (void)transport_send_control(ctx,
                                         handle,
                                         source_id,
                                         XGL_TRANSPORT_CONTROL_NACK,
                                         expected_seq,
                                         packet->session_id);
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_SEQUENCE_ERROR;
        }

        /* Mark sequence number as received and ACK it. */
        err = xgl_ack_mark_received_from(&ctx->ack_handler, source_id, seq_num);
        if (err != XGL_OK) {
            return err;
        }

        err = xgl_ack_update_expected_from(&ctx->ack_handler, source_id, seq_num);
        if (err != XGL_OK) {
            return err;
        }

        transport_send_ack(ctx, handle, seq_num, source_id, packet->session_id);
    }
    
    /* Check if packet has fragment flag */
    bool is_fragment = packet->fragment;
    
    /* Handle fragmented packets */
    if (is_fragment && ctx->fragment_mgr) {
        uint8_t* complete_data = NULL;
        size_t complete_len = 0;
        
        err = xgl_fragment_process(ctx->fragment_mgr, source_id, data_type,
                                   data, data_len, &complete_data, &complete_len, 0);
        
        if (err == XGL_OK) {
            /* Reassembly complete, deliver to application */
            if (ctx->rx_callback) {
                ctx->rx_callback(handle, source_id, data_type, 
                               complete_data, complete_len, ctx->callback_user_data);
            }
            
            /* Free complete data */
            xgl_fragment_free_data(ctx->fragment_mgr, complete_data);
            
            /* Update statistics */
            ctx->stats->rx_packets++;
            ctx->stats->rx_bytes += complete_len;
        } else if (err == XGL_ERR_BUSY) {
            /* Waiting for more fragments */
            return XGL_OK;
        } else {
            /* Error in reassembly */
            return err;
        }
    } else {
        /* Non-fragmented packet, deliver to application */
        if (ctx->rx_callback) {
            ctx->rx_callback(handle, source_id, data_type,
                           data, data_len, ctx->callback_user_data);
        }
        
        /* Update statistics */
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += data_len;
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
 * \brief           Get next sequence number for target
 */
uint8_t xgl_transport_get_next_seq(xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return 0;
    }
    return xgl_window_get_next_seq(&ctx->window);
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
