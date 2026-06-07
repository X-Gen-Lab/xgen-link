/**
 * \file            xgl_network.c
 * \brief           Network layer implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_network.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_route.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_config.h>
#include <string.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Extract packet information from frame buffer
 * \details         Parses frame header and extracts packet metadata
 */
static xgl_error_t xgl_network_extract_packet_info(const uint8_t* frame_buf,
                                                   size_t frame_len,
                                                   uint8_t* source_id,
                                                   uint8_t* target_id,
                                                   uint8_t* data_type,
                                                   const uint8_t** payload,
                                                   size_t* payload_len) {
    /* Validate minimum frame size */
    if (frame_len < XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Decode frame header */
    xgl_frame_header_t header;
    xgl_frame_decode_header(&header, frame_buf);
    
    /* Extract addressing */
    *source_id = header.source_id;
    *target_id = header.target_id;
    *data_type = xgl_frame_get_datatype(&header);
    
    /* Extract payload */
    *payload = frame_buf + XGL_FRAME_HEADER_SIZE;
    *payload_len = header.data_len;
    
    /* Validate payload length */
    if (frame_len < XGL_FRAME_HEADER_SIZE + *payload_len + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Public API Implementation                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize network layer context
 */
xgl_error_t xgl_network_init(xgl_network_ctx_t* ctx,
                             const xgl_network_config_t* config) {
    if (ctx == NULL || config == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (config->route_table == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_network_ctx_t));
    ctx->local_id = config->local_id;
    ctx->route_table = config->route_table;
    ctx->upper_layer = config->upper_layer;
    ctx->lower_layer = config->lower_layer;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->stats = config->stats;
    
    return XGL_OK;
}

/**
 * \brief           Send packet through network layer
 * \details         Performs route lookup and forwards packet to data link layer
 */
static xgl_error_t xgl_network_send_with_handle(xgl_network_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                xgl_packet_t* packet,
                                                bool assign_seq) {
    if (ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Validate packet data */
    if (packet->data == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Lookup route for target ID */
    xgl_route_item_t* route = xgl_route_table_lookup(ctx->route_table,
                                                     packet->target_id);
    
    if (route == NULL) {
        /* Route not found - report error */
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg),
                "Route not found for target ID: %u", packet->target_id);
        
        if (ctx->error_callback != NULL) {
            ctx->error_callback(handle, XGL_ERR_ROUTE_NOT_FOUND,
                              error_msg, ctx->callback_user_data);
        }
        
        /* Update statistics */
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        
        return XGL_ERR_ROUTE_NOT_FOUND;
    }
    
    /* Store PHY operations in packet for transmission */
    packet->phy = route->phy;
    
    /* Set source ID if not already set */
    if (packet->source_id == 0) {
        packet->source_id = ctx->local_id;
    }
    
    /* Set protocol version */
    packet->version = XGL_PROTOCOL_VERSION;
    
    /* Assign sequence number if requested */
    /* Note: Sequence number assignment is typically done by transport layer */
    /* This is just a placeholder for network layer forwarding */
    (void)assign_seq;  /* Unused in this layer */
    
    /* Update statistics */
    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        if (packet->data != NULL) {
            ctx->stats->tx_bytes += packet->data->data_len;
        }
    }
    
    /* Build frame from packet for datalink transmission */
    xgl_frame_t frame;
    uint8_t reliable_type = XGL_ATTR_RELIABLE_NONE;
    if (packet->reliable == XGL_ATTR_RELIABLE_ACK) {
        reliable_type = XGL_ATTR_RELIABLE_ACK;
    } else if (packet->reliable != 0U) {
        reliable_type = XGL_ATTR_RELIABLE_TX;
    }

    xgl_frame_params_t params = {
        .source_id = packet->source_id,
        .target_id = packet->target_id,
        .data_type = packet->data_type,
        .seq_num = packet->seq_num,
        .ack_num = packet->ack_num,
        .payload = packet->data->data,
        .payload_len = packet->data->data_len,
        .reliable = packet->reliable,
        .reliable_type = reliable_type,
        .fragment = packet->fragment,
        .priority = packet->priority,
        .session_id = packet->session_id,
        .ttl = XGL_DEFAULT_TTL
    };
    
    xgl_error_t err = xgl_frame_build(&frame, &params);
    
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return err;
    }

    if (xgl_frame_calculate_size(frame.payload_len) > route->max_frame_size) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Send frame through data link layer via interface */
    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        /* No datalink interface available, cannot send */
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Prepare frame data for lower layer */
    xgl_frame_tx_message_t send_data = {
        .frame = &frame,
        .phy = packet->phy
    };
    
    err = ctx->lower_layer->send(ctx->lower_layer->ctx, handle, &send_data);
    
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return err;
    }
    
    return XGL_OK;
}

xgl_error_t xgl_network_send(xgl_network_ctx_t* ctx,
                             xgl_packet_t* packet,
                             bool assign_seq) {
    return xgl_network_send_with_handle(ctx, NULL, packet, assign_seq);
}

/**
 * \brief           Receive and process packet from data link layer
 * \details         Validates addressing and forwards to appropriate handler
 */
xgl_error_t xgl_network_receive(xgl_network_ctx_t* ctx,
                                xgl_handle_t handle,
                                const uint8_t* frame_buf,
                                size_t frame_len) {
    if (ctx == NULL || frame_buf == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Extract packet information from frame */
    uint8_t source_id, target_id, data_type;
    const uint8_t* payload;
    size_t payload_len;
    
    xgl_error_t err = xgl_network_extract_packet_info(frame_buf, frame_len,
                                                      &source_id, &target_id,
                                                      &data_type,
                                                      &payload, &payload_len);
    if (err != XGL_OK) {
        /* Invalid frame - update statistics and return error */
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        return err;
    }
    
    /* Validate addressing */
    if (!xgl_network_validate_address(ctx, target_id, source_id)) {
        /* Invalid address - drop packet */
        if (ctx->stats != NULL) {
            ctx->stats->rx_dropped++;
        }
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Check if packet is addressed to local node */
    if (xgl_network_is_local(ctx, target_id)) {
        /* Packet is for this node - forward to transport layer */
        
        /* Update statistics */
        if (ctx->stats != NULL) {
            ctx->stats->rx_packets++;
            ctx->stats->rx_bytes += payload_len;
        }
        
        /* Forward to transport layer via interface */
        if (ctx->upper_layer != NULL && ctx->upper_layer->receive != NULL) {
            /* Extract frame attributes and build packet structure */
            xgl_frame_header_t header;
            xgl_frame_decode_header(&header, frame_buf);
            
            /* Build packet data structure for payload */
            xgl_packet_data_t packet_data = {
                .ref_count = 1,
                .data_len = payload_len,
                .data = payload,
                .owned_data = NULL
            };
            
            /* Build complete packet structure for transport layer */
            xgl_packet_t packet = {
                .source_id = source_id,
                .target_id = target_id,
                .data_type = data_type,
                .seq_num = header.seq_num,
                .ack_num = header.ack_num,
                .session_id = (uint16_t)(header.attr_msb & XGL_ATTR_SESSION_MASK),
                .reliable = header.attr_lsb & XGL_ATTR_RELIABLE_MASK,
                .fragment = (header.attr_lsb & XGL_ATTR_FRAGMENT_MASK) >> XGL_ATTR_FRAGMENT_SHIFT,
                .priority = (header.attr_lsb & XGL_ATTR_PRIORITY_MASK) >> XGL_ATTR_PRIORITY_SHIFT,
                .data = &packet_data,
                .phy = NULL    /* Not needed for receive path */
            };
            
            /* Call transport layer receive via interface */
            err = ctx->upper_layer->receive(ctx->upper_layer->ctx, handle, &packet);
            if (err != XGL_OK) {
                /* Transport layer processing failed */
                return err;
            }
        }
        
        return XGL_OK;
    } else {
        /* Packet is for another node - forward it */
        /* Lookup route for forwarding */
        xgl_route_item_t* route = xgl_route_table_lookup(ctx->route_table,
                                                         target_id);
        
        if (route == NULL) {
            /* No route for forwarding - drop packet */
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg),
                    "No route for forwarding to target ID: %u", target_id);
            
            if (ctx->error_callback != NULL) {
                ctx->error_callback(handle, XGL_ERR_ROUTE_NOT_FOUND,
                                  error_msg, ctx->callback_user_data);
            }
            
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            
            return XGL_ERR_ROUTE_NOT_FOUND;
        }

        xgl_frame_header_t header;
        xgl_frame_decode_header(&header, frame_buf);

        if (header.reserved == 0U) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            if (ctx->error_callback != NULL) {
                ctx->error_callback(handle, XGL_ERR_TTL_EXPIRED,
                                  "Packet TTL expired",
                                  ctx->callback_user_data);
            }
            return XGL_ERR_TTL_EXPIRED;
        }
        
        /* Forward packet through the appropriate PHY */
        /* Note: In a full implementation, this would re-transmit the frame */
        /* For now, we just acknowledge that forwarding would occur */
        
        /* Update statistics for forwarded packet */
        if (ctx->stats != NULL) {
            ctx->stats->tx_packets++;
            ctx->stats->tx_bytes += payload_len;
        }
        
        if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_BUFFER_TOO_SMALL;
        }

        uint8_t forward_buf[XGL_DATALINK_MAX_FRAME_SIZE];
        memcpy(forward_buf, frame_buf, frame_len);
        forward_buf[10] = (uint8_t)(header.reserved - 1U);
        forward_buf[11] = xgl_crc8_maxim(forward_buf, XGL_FRAME_HEADER_SIZE - 1U);
        uint16_t forward_crc = xgl_crc16_modbus(forward_buf, frame_len - XGL_CRC16_SIZE);
        xgl_serialize_u16_le(&forward_buf[frame_len - XGL_CRC16_SIZE], forward_crc);

        /* Forward the frame through the PHY */
        if (route->phy != NULL && route->phy->tx != NULL) {
            err = route->phy->tx(forward_buf, frame_len, route->phy->user_data);
            if (err != XGL_OK) {
                if (ctx->stats != NULL) {
                    ctx->stats->tx_errors++;
                }
                return XGL_ERR_TX_FAILED;
            }
        }
        
        return XGL_OK;
    }
}

/**
 * \brief           Validate packet addressing
 * \details         Checks if source and target IDs are valid
 */
bool xgl_network_validate_address(const xgl_network_ctx_t* ctx,
                                  uint8_t target_id,
                                  uint8_t source_id) {
    if (ctx == NULL) {
        return false;
    }
    
    /* Source ID should not be broadcast */
    if (source_id == XGL_BROADCAST_ID) {
        return false;
    }
    
    /* Source ID should not be zero (reserved) */
    if (source_id == 0) {
        return false;
    }
    
    /* Target ID can be broadcast or specific node */
    /* Zero is reserved but we allow it for special cases */
    
    /* Prevent self-addressing (source == target) unless broadcast */
    if (source_id == target_id && target_id != XGL_BROADCAST_ID) {
        /* Allow loopback for testing purposes */
        /* In production, this might be disallowed */
    }
    
    return true;
}

/**
 * \brief           Invoke error callback
 * \details         Reports error through registered callback
 */
void xgl_network_report_error(xgl_network_ctx_t* ctx,
                              xgl_handle_t handle,
                              xgl_error_t error,
                              const char* message) {
    if (ctx == NULL) {
        return;
    }
    
    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle, error, message, ctx->callback_user_data);
    }
    
    /* Update error statistics */
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

/*---------------------------------------------------------------------------*/
/* Layer Interface Implementation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Network layer send implementation (called by upper layers)
 * \details         This function is called by transport layer to send packets
 */
static xgl_error_t network_send_impl(void* ctx,
                                    xgl_handle_t handle,
                                    void* data) {
    xgl_network_ctx_t* net_ctx = (xgl_network_ctx_t*)ctx;
    xgl_packet_t* packet = (xgl_packet_t*)data;
    
    if (net_ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Forward to network send function */
    return xgl_network_send_with_handle(net_ctx, handle, packet, false);
}

/**
 * \brief           Network layer receive implementation (called by lower layers)
 * \details         This function is called by datalink layer to deliver frames
 */
static xgl_error_t network_receive_impl(void* ctx,
                                       xgl_handle_t handle,
                                       void* data) {
    xgl_network_ctx_t* net_ctx = (xgl_network_ctx_t*)ctx;
    
    if (net_ctx == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Extract frame buffer and length from data */
    xgl_frame_rx_message_t* frame_data = (xgl_frame_rx_message_t*)data;
    
    /* Forward to network receive function */
    return xgl_network_receive(net_ctx, handle, frame_data->frame_buf, frame_data->frame_len);
}

/**
 * \brief           Network layer error reporting implementation
 * \details         This function is called to report errors to upper layers
 */
static xgl_error_t network_report_error_impl(void* ctx,
                                            xgl_handle_t handle,
                                            void* data) {
    xgl_network_ctx_t* net_ctx = (xgl_network_ctx_t*)ctx;
    xgl_layer_error_info_t* error_info = (xgl_layer_error_info_t*)data;
    
    if (net_ctx == NULL || error_info == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Forward error to callback if available */
    if (net_ctx->error_callback != NULL) {
        net_ctx->error_callback(handle, error_info->error,
                               error_info->message,
                               net_ctx->callback_user_data);
    }
    
    return XGL_OK;
}

/**
 * \brief           Get network layer interface
 * \details         Returns the layer interface for this network instance
 * \param[in]       ctx: Network layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_network_get_interface(xgl_network_ctx_t* ctx,
                                     xgl_layer_interface_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    xgl_layer_interface_init(iface,
                            ctx,
                            network_send_impl,
                            network_receive_impl,
                            network_report_error_impl);
    
    return XGL_OK;
}
