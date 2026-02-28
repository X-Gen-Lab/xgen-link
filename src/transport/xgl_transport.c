/**
 * \file            xgl_transport.c
 * \brief           Transport Layer Main Interface Implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_transport.h"
#include "xgl/xgl_network.h"
#include "xgl/xgl_packet_pool.h"
#include "xgl/xgl_route.h"
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

/*---------------------------------------------------------------------------*/
/* Transport Layer Initialization                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport layer context
 */
xgl_error_t xgl_transport_init(xgl_transport_ctx_t* ctx,
                               uint8_t local_id,
                               uint8_t max_retry_count,
                               uint32_t default_timeout_ms,
                               uint8_t window_size,
                               bool enable_fragmentation,
                               uint16_t max_frame_size,
                               xgl_network_ctx_t* network_ctx,
                               xgl_rx_callback_t rx_callback,
                               xgl_error_callback_t error_callback,
                               void* callback_user_data,
                               xgl_statistics_t* stats,
                               xgl_allocator_t* allocator) {
    xgl_error_t err;
    
    if (!ctx || !network_ctx || !stats) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
    ctx->local_id = local_id;
    ctx->max_retry_count = max_retry_count;
    ctx->default_timeout_ms = default_timeout_ms;
    ctx->enable_fragmentation = enable_fragmentation;
    ctx->max_frame_size = max_frame_size;
    ctx->network_ctx = network_ctx;
    ctx->rx_callback = rx_callback;
    ctx->error_callback = error_callback;
    ctx->callback_user_data = callback_user_data;
    ctx->stats = stats;
    ctx->allocator = allocator;
    
    /* Initialize RTT estimator */
    xgl_rtt_init(&ctx->rtt_est);
    
    /* Initialize sliding window */
    err = xgl_window_init(&ctx->window, window_size);
    if (err != XGL_OK) {
        return err;
    }
    
    /* Initialize reliable transmission queue */
    err = xgl_reliable_init(&ctx->reliable_queue, max_retry_count, allocator);
    if (err != XGL_OK) {
        xgl_window_destroy(&ctx->window);
        return err;
    }
    
    /* Initialize ACK handler */
    err = xgl_ack_init(&ctx->ack_handler, allocator);
    if (err != XGL_OK) {
        xgl_reliable_destroy(&ctx->reliable_queue);
        xgl_window_destroy(&ctx->window);
        return err;
    }
    
    /* Initialize fragmentation manager if enabled */
    if (enable_fragmentation) {
        ctx->fragment_mgr = (xgl_fragment_manager_t*)transport_malloc(allocator, 
                                                                       sizeof(xgl_fragment_manager_t));
        if (!ctx->fragment_mgr) {
            xgl_ack_destroy(&ctx->ack_handler);
            xgl_reliable_destroy(&ctx->reliable_queue);
            xgl_window_destroy(&ctx->window);
            return XGL_ERR_NO_MEMORY;
        }
        
        err = xgl_fragment_init(ctx->fragment_mgr, 8, XGL_FRAGMENT_TIMEOUT_MS, allocator);
        if (err != XGL_OK) {
            transport_free(allocator, ctx->fragment_mgr);
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
    
    /* Check if window allows sending for reliable transmission */
    if (tx_data->reliable && !xgl_window_can_send(&ctx->window)) {
        return XGL_ERR_WINDOW_FULL;
    }
    
    /* Determine if fragmentation is needed */
    size_t max_payload_size = ctx->max_frame_size - XGL_FRAME_HEADER_SIZE - XGL_CRC16_SIZE;
    bool needs_fragmentation = (tx_data->data_len > max_payload_size) && ctx->enable_fragmentation;
    
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
            uint8_t seq_num = xgl_window_get_next_seq(&ctx->window);
            xgl_window_advance_next_seq(&ctx->window);
            
            /* Get timeout */
            int32_t timeout_ms = xgl_rtt_get_rto(&ctx->rtt_est);
            if (timeout_ms == 0) {
                timeout_ms = ctx->default_timeout_ms;
            }
            
            /* Find route */
            xgl_route_item_t* route = xgl_route_table_lookup(ctx->network_ctx->route_table, 
                                                             tx_data->target_id);
            if (!route) {
                /* Clean up fragments */
                xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
                transport_free(ctx->allocator, fragments);
                transport_free(ctx->allocator, fragment_lens);
                return XGL_ERR_ROUTE_NOT_FOUND;
            }
            xgl_phy_ops_t* phy = route->phy;
            
            /* Add to reliable queue if needed */
            if (tx_data->reliable) {
                err = xgl_reliable_add_packet(&ctx->reliable_queue,
                                             fragments[i], fragment_lens[i],
                                             ctx->local_id, tx_data->target_id,
                                             seq_num, tx_data->data_type,
                                             tx_data->priority, timeout_ms, phy);
                if (err != XGL_OK) {
                    xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
                    transport_free(ctx->allocator, fragments);
                    transport_free(ctx->allocator, fragment_lens);
                    return err;
                }
            }
            
            /* TODO: Send fragment through network layer */
            /* This requires creating a packet structure and calling xgl_network_send */
        }
        
        /* Clean up */
        xgl_fragment_free_fragments(ctx->fragment_mgr, fragments, fragment_count);
        transport_free(ctx->allocator, fragments);
        transport_free(ctx->allocator, fragment_lens);
        
    } else {
        /* Send without fragmentation */
        
        /* Get sequence number */
        uint8_t seq_num = 0;
        if (tx_data->reliable) {
            seq_num = xgl_window_get_next_seq(&ctx->window);
            xgl_window_advance_next_seq(&ctx->window);
        }
        
        /* Get timeout */
        int32_t timeout_ms = xgl_rtt_get_rto(&ctx->rtt_est);
        if (timeout_ms == 0) {
            timeout_ms = ctx->default_timeout_ms;
        }
        
        /* Find route */
        xgl_route_item_t* route = xgl_route_table_lookup(ctx->network_ctx->route_table, 
                                                         tx_data->target_id);
        if (!route) {
            return XGL_ERR_ROUTE_NOT_FOUND;
        }
        xgl_phy_ops_t* phy = route->phy;
        
        /* Add to reliable queue if needed */
        if (tx_data->reliable) {
            err = xgl_reliable_add_packet(&ctx->reliable_queue,
                                         tx_data->data, tx_data->data_len,
                                         ctx->local_id, tx_data->target_id,
                                         seq_num, tx_data->data_type,
                                         tx_data->priority, timeout_ms, phy);
            if (err != XGL_OK) {
                return err;
            }
        }
        
        /* TODO: Send packet through network layer */
        /* This requires creating a packet structure and calling xgl_network_send */
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
                                  uint8_t source_id,
                                  uint8_t target_id,
                                  uint8_t seq_num,
                                  uint8_t ack_num,
                                  uint8_t data_type,
                                  uint8_t reliable,
                                  uint8_t fragment,
                                  const uint8_t* data,
                                  size_t data_len) {
    xgl_error_t err;
    
    (void)target_id;  /* Unused parameter */
    
    if (!ctx || !data) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check if this is an ACK packet */
    if (reliable == XGL_ATTR_RELIABLE_ACK) {
        /* Process ACK */
        bool is_valid = false;
        err = xgl_ack_process(&ctx->ack_handler, ack_num, source_id, &is_valid);
        if (err != XGL_OK || !is_valid) {
            return err;
        }
        
        /* Remove packet from reliable queue */
        err = xgl_reliable_remove_packet(&ctx->reliable_queue, ack_num, source_id);
        if (err == XGL_OK) {
            /* Update RTT estimate */
            xgl_reliable_packet_t* packet = xgl_reliable_find_packet(&ctx->reliable_queue, 
                                                                     ack_num, source_id);
            if (packet) {
                /* Calculate RTT (current_time - send_timestamp) */
                /* Note: We need current time here, but it's not passed in */
                /* For now, we'll skip RTT update */
            }
            
            /* Advance sliding window */
            xgl_window_mark_ack(&ctx->window, ack_num);
            xgl_window_advance_base(&ctx->window);
        }
        
        return XGL_OK;
    }
    
    /* Check for duplicate packet */
    if (xgl_ack_is_duplicate(&ctx->ack_handler, seq_num)) {
        /* Send ACK for duplicate */
        if (reliable == XGL_ATTR_RELIABLE_TX) {
            uint8_t ack_buffer[64];
            size_t ack_len;
            err = xgl_ack_generate(seq_num, source_id, ctx->local_id, 
                                  ack_buffer, sizeof(ack_buffer), &ack_len);
            if (err == XGL_OK) {
                /* TODO: Send ACK through network layer */
            }
        }
        return XGL_OK;
    }
    
    /* Mark sequence number as received */
    xgl_ack_mark_received(&ctx->ack_handler, seq_num);
    
    /* Send ACK if reliable transmission */
    if (reliable == XGL_ATTR_RELIABLE_TX) {
        uint8_t ack_buffer[64];
        size_t ack_len;
        err = xgl_ack_generate(seq_num, source_id, ctx->local_id,
                              ack_buffer, sizeof(ack_buffer), &ack_len);
        if (err == XGL_OK) {
            /* TODO: Send ACK through network layer */
        }
    }
    
    /* Handle fragmented packets */
    if (fragment && ctx->fragment_mgr) {
        uint8_t* complete_data = NULL;
        size_t complete_len = 0;
        
        err = xgl_fragment_process(ctx->fragment_mgr, source_id, data_type,
                                   data, data_len, &complete_data, &complete_len);
        
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
    
    /* Process reliable transmission timeouts */
    xgl_reliable_packet_t* retry_exhausted = NULL;
    uint32_t retransmit_count = xgl_reliable_process_timeouts(&ctx->reliable_queue,
                                                               current_time_ms,
                                                               &retry_exhausted);
    
    /* Handle retry exhausted packets */
    if (retry_exhausted) {
        /* Report error */
        if (ctx->error_callback) {
            ctx->error_callback(handle, XGL_ERR_ACK_TIMEOUT,
                              "Packet retry count exhausted",
                              ctx->callback_user_data);
        }
        
        /* Update statistics */
        ctx->stats->tx_errors++;
    }
    
    /* Update retransmission statistics */
    if (retransmit_count > 0) {
        ctx->stats->tx_retries += retransmit_count;
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
    return xgl_window_can_send(&ctx->window);
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
