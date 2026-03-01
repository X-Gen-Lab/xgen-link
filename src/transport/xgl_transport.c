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
                                     uint8_t source_id) {
    xgl_error_t err;
    uint8_t ack_buffer[XGL_ACK_BUFFER_SIZE];
    size_t ack_len;
    
    err = xgl_ack_generate(seq_num, source_id, ctx->local_id,
                          ack_buffer, sizeof(ack_buffer), &ack_len);
    if (err != XGL_OK) {
        return err;
    }
    
    /* Send ACK through network layer */
    xgl_packet_data_t ack_packet_data = {
        .ref_count = 1,
        .data_len = ack_len,
        .data = ack_buffer
    };
    
    xgl_packet_t ack_packet = {
        .source_id = ctx->local_id,
        .target_id = source_id,
        .data_type = 0xFF,  /* ACK data type */
        .seq_num = 0,
        .ack_num = seq_num,
        .reliable = false,
        .fragment = false,  /* ACKs are never fragmented */
        .priority = 7,  /* Highest priority */
        .data = &ack_packet_data,
        .phy = NULL  /* Will be set by network layer */
    };
    
    if (ctx->lower_layer != NULL && ctx->lower_layer->send != NULL) {
        xgl_layer_send(ctx->lower_layer, handle, &ack_packet);
    }
    
    return XGL_OK;
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
    
    /* Check if window allows sending for reliable transmission */
    if (tx_data->reliable && !xgl_window_can_send(&ctx->window)) {
        return XGL_ERR_WINDOW_FULL;
    }
    
    /* Validate frame size before calculating payload size */
    size_t min_frame_size = XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE;
    if (ctx->max_frame_size < min_frame_size) {
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
            
            /* Get timeout - use custom timeout if provided, otherwise use RTT estimate or default */
            int32_t timeout_ms;
            if (tx_data->timeout_ms > 0) {
                timeout_ms = (int32_t)tx_data->timeout_ms;
            } else {
                timeout_ms = xgl_rtt_get_rto(&ctx->rtt_est);
                if (timeout_ms == 0) {
                    timeout_ms = ctx->default_timeout_ms;
                }
            }
            
            /* Note: Route lookup is now handled by network layer */
            /* The PHY will be determined when the packet reaches network layer */
            xgl_phy_ops_t* phy = NULL;  /* Will be set by network layer */
            
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
            
            /* Send fragment through network layer via interface */
            xgl_packet_data_t packet_data = {
                .ref_count = 1,
                .data_len = fragment_lens[i],
                .data = fragments[i]
            };
            
            xgl_packet_t packet = {
                .source_id = ctx->local_id,
                .target_id = tx_data->target_id,
                .data_type = tx_data->data_type,
                .seq_num = seq_num,
                .ack_num = 0,  /* ACK number is 0 for data packets */
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
        uint8_t seq_num = 0;
        if (tx_data->reliable) {
            seq_num = xgl_window_get_next_seq(&ctx->window);
            xgl_window_advance_next_seq(&ctx->window);
        }
        
        /* Get timeout - use custom timeout if provided, otherwise use RTT estimate or default */
        int32_t timeout_ms;
        if (tx_data->timeout_ms > 0) {
            timeout_ms = (int32_t)tx_data->timeout_ms;
        } else {
            timeout_ms = xgl_rtt_get_rto(&ctx->rtt_est);
            if (timeout_ms == 0) {
                timeout_ms = ctx->default_timeout_ms;
            }
        }
        
        /* Create packet data structure for network layer */
        xgl_packet_data_t packet_data = {
            .ref_count = 1,
            .data_len = tx_data->data_len,
            .data = (uint8_t*)tx_data->data  /* Cast away const for packet structure */
        };
        
        xgl_packet_t packet = {
            .source_id = ctx->local_id,
            .target_id = tx_data->target_id,
            .data_type = tx_data->data_type,
            .seq_num = seq_num,
            .ack_num = 0,  /* ACK number is 0 for data packets */
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
            err = xgl_reliable_add_packet(&ctx->reliable_queue,
                                         tx_data->data, tx_data->data_len,
                                         ctx->local_id, tx_data->target_id,
                                         seq_num, tx_data->data_type,
                                         tx_data->priority, timeout_ms, packet.phy);
            if (err != XGL_OK) {
                return err;
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
    uint8_t source_id = packet->source_id;
    uint8_t seq_num = packet->seq_num;
    uint8_t ack_num = packet->ack_num;
    uint8_t data_type = packet->data_type;
    uint8_t reliable = packet->reliable;
    
    /* Extract payload data from packet */
    const uint8_t* data = NULL;
    size_t data_len = 0;
    if (packet->data != NULL) {
        data = packet->data->data;
        data_len = packet->data->data_len;
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
            xgl_reliable_packet_t* rel_packet = xgl_reliable_find_packet(&ctx->reliable_queue, 
                                                                          ack_num, source_id);
            if (rel_packet) {
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
    
    /* Validate data pointer for non-ACK packets */
    if (data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check for duplicate packet */
    if (xgl_ack_is_duplicate(&ctx->ack_handler, seq_num)) {
        /* Send ACK for duplicate - sender may have missed our previous ACK */
        if (reliable == XGL_ATTR_RELIABLE_TX) {
            transport_send_ack(ctx, handle, seq_num, source_id);
        }
        return XGL_OK;  /* Discard duplicate, don't process again */
    }
    
    /* Mark sequence number as received */
    xgl_ack_mark_received(&ctx->ack_handler, seq_num);
    
    /* Send ACK if reliable transmission */
    if (reliable == XGL_ATTR_RELIABLE_TX) {
        transport_send_ack(ctx, handle, seq_num, source_id);
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
