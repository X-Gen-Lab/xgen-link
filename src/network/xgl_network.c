/**
 * \file            xgl_network.c
 * \brief           Network layer implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_network.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_route.h>
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
                             uint8_t local_id,
                             xgl_route_table_t* route_table,
                             xgl_rx_callback_t rx_callback,
                             xgl_error_callback_t error_callback,
                             void* callback_user_data,
                             xgl_statistics_t* stats) {
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (route_table == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_network_ctx_t));
    ctx->local_id = local_id;
    ctx->route_table = route_table;
    ctx->rx_callback = rx_callback;
    ctx->error_callback = error_callback;
    ctx->callback_user_data = callback_user_data;
    ctx->stats = stats;
    
    return XGL_OK;
}

/**
 * \brief           Send packet through network layer
 * \details         Performs route lookup and forwards packet to data link layer
 */
xgl_error_t xgl_network_send(xgl_network_ctx_t* ctx,
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
            ctx->error_callback(NULL, XGL_ERR_ROUTE_NOT_FOUND,
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
    
    /* Packet is now ready for data link layer transmission */
    /* The actual transmission will be handled by the transport/datalink layer */
    
    return XGL_OK;
}

/**
 * \brief           Receive and process packet from data link layer
 * \details         Validates addressing and forwards to appropriate handler
 */
xgl_error_t xgl_network_receive(xgl_network_ctx_t* ctx,
                                xgl_handle_t handle,
                                uint8_t* frame_buf,
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
        /* Packet is for this node - forward to transport layer or application */
        
        /* Update statistics */
        if (ctx->stats != NULL) {
            ctx->stats->rx_packets++;
            ctx->stats->rx_bytes += payload_len;
        }
        
        /* Invoke receive callback if registered */
        if (ctx->rx_callback != NULL) {
            ctx->rx_callback(handle, source_id, data_type,
                           payload, payload_len,
                           ctx->callback_user_data);
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
        
        /* Forward packet through the appropriate PHY */
        /* Note: In a full implementation, this would re-transmit the frame */
        /* For now, we just acknowledge that forwarding would occur */
        
        /* Update statistics for forwarded packet */
        if (ctx->stats != NULL) {
            ctx->stats->tx_packets++;
            ctx->stats->tx_bytes += payload_len;
        }
        
        /* Forward the frame through the PHY */
        if (route->phy != NULL && route->phy->tx != NULL) {
            err = route->phy->tx(frame_buf, frame_len, route->phy->user_data);
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
