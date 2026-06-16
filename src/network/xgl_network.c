/**
 * \file            xgl_network.c
 * \brief           Network layer implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/xgl_error.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_crc.h>
#include <xgl/internal/xgl_serialize.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_config.h>
#include <string.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint8_t data_type;
    bool data_type_found;
    uint32_t session_epoch;
    bool session_epoch_found;
} network_ext_metadata_t;

static xgl_error_t network_decode_ext_metadata(const uint8_t* extensions,
                                               size_t extensions_len,
                                               network_ext_metadata_t* metadata) {
    if (metadata == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(metadata, 0, sizeof(*metadata));
    if (extensions == NULL || extensions_len == 0U) {
        return XGL_OK;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&cursor, extensions, extensions_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type == XGL_WIRE_EXT_DATA_TYPE) {
            if (metadata->data_type_found || ext.len != 1U || ext.value == NULL) {
                return XGL_ERR_INVALID_FRAME;
            }
            metadata->data_type = ext.value[0];
            metadata->data_type_found = true;
            continue;
        }
        if (ext.type == XGL_WIRE_EXT_SESSION) {
            if (metadata->session_epoch_found) {
                return XGL_ERR_INVALID_FRAME;
            }
            uint64_t incarnation_id = 0U;
            err = xgl_wire_decode_session_ext_value(ext.value,
                                                    ext.len,
                                                    &metadata->session_epoch,
                                                    &incarnation_id);
            if (err != XGL_OK) {
                return err;
            }
            (void)incarnation_id;
            metadata->session_epoch_found = true;
        }
    }

    return (err == XGL_ERR_NOT_FOUND) ? XGL_OK : err;
}

static xgl_error_t network_append_data_type_ext(uint8_t* extensions,
                                                size_t extensions_capacity,
                                                size_t* extensions_len,
                                                uint8_t data_type) {
    if (extensions == NULL || extensions_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (data_type == 0U) {
        return XGL_OK;
    }
    if (*extensions_len > extensions_capacity) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t bytes_written = 0U;
    xgl_error_t err = xgl_wire_encode_ext(&extensions[*extensions_len],
                                          extensions_capacity - *extensions_len,
                                          XGL_WIRE_EXT_DATA_TYPE,
                                          &data_type,
                                          1U,
                                          &bytes_written);
    if (err != XGL_OK) {
        return err;
    }
    *extensions_len += bytes_written;
    return XGL_OK;
}

static xgl_error_t network_copy_packet_extensions(uint8_t* extensions,
                                                  size_t extensions_capacity,
                                                  size_t* extensions_len,
                                                  const xgl_packet_t* packet) {
    if (extensions == NULL || extensions_len == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *extensions_len = 0U;
    if (packet->extensions != NULL && packet->extensions_len > 0U) {
        if (packet->extensions_len > extensions_capacity) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(extensions, packet->extensions, packet->extensions_len);
        *extensions_len = packet->extensions_len;
    }

    network_ext_metadata_t metadata;
    xgl_error_t err = network_decode_ext_metadata(extensions,
                                                  *extensions_len,
                                                  &metadata);
    if (err != XGL_OK) {
        return err;
    }
    if (metadata.data_type_found) {
        if (packet->data_type != 0U && packet->data_type != metadata.data_type) {
            return XGL_ERR_INVALID_PARAM;
        }
        return XGL_OK;
    }

    return network_append_data_type_ext(extensions,
                                        extensions_capacity,
                                        extensions_len,
                                        packet->data_type);
}

/**
 * \brief           Extract packet information from frame buffer
 * \details         Parses frame header and extracts packet metadata
 */
static xgl_error_t xgl_network_extract_packet_info(const uint8_t* frame_buf,
                                                   size_t frame_len,
                                                   uint16_t* source_id,
                                                   uint16_t* target_id,
                                                   uint8_t* data_type,
                                                   uint32_t* session_epoch,
                                                   const uint8_t** payload,
                                                   size_t* payload_len) {
    /* Validate minimum frame size */
    if (frame_len < XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_wire_header_t wire_header;
    if (xgl_wire_decode_header(&wire_header, frame_buf, frame_len) != XGL_OK) {
        return XGL_ERR_INVALID_FRAME;
    }

    /* Extract addressing */
    *source_id = wire_header.source_id;
    *target_id = wire_header.target_id;
    const uint8_t* extensions = NULL;
    size_t extensions_len = 0U;
    if (wire_header.header_len > XGL_WIRE_BASE_HEADER_SIZE) {
        extensions = frame_buf + XGL_WIRE_BASE_HEADER_SIZE;
        extensions_len = wire_header.header_len - XGL_WIRE_BASE_HEADER_SIZE;
    }
    network_ext_metadata_t metadata;
    xgl_error_t err = network_decode_ext_metadata(extensions,
                                                  extensions_len,
                                                  &metadata);
    if (err != XGL_OK) {
        return err;
    }
    *data_type = metadata.data_type;
    *session_epoch = metadata.session_epoch;

    /* Extract payload */
    *payload = frame_buf + wire_header.header_len;
    *payload_len = wire_header.payload_len;

    /* Validate payload length */
    if (frame_len < wire_header.header_len + *payload_len + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

static xgl_error_t network_find_security_ext(const uint8_t* frame_buf,
                                             const xgl_wire_header_t* header,
                                             uint32_t* key_id,
                                             uint8_t* tag_len) {
    if (frame_buf == NULL || header == NULL || key_id == NULL || tag_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *key_id = 0U;
    *tag_len = 0U;
    if (header->header_len <= XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_NOT_FOUND;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(
        &cursor,
        &frame_buf[XGL_WIRE_BASE_HEADER_SIZE],
        header->header_len - XGL_WIRE_BASE_HEADER_SIZE
    );
    if (err != XGL_OK) {
        return err;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type != XGL_WIRE_EXT_SECURITY) {
            continue;
        }

        uint64_t nonce_id = 0U;
        return xgl_wire_decode_security_ext_value(ext.value,
                                                  ext.len,
                                                  key_id,
                                                  &nonce_id,
                                                  tag_len);
    }

    return err;
}

static xgl_error_t network_resign_forwarded_frame(xgl_network_ctx_t* ctx,
                                                  uint8_t* frame_buf,
                                                  size_t frame_len,
                                                  const xgl_wire_header_t* header) {
    if (ctx == NULL || frame_buf == NULL || header == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    bool authenticated = (header->flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U;
    if (!authenticated) {
        return ctx->auth_required ? XGL_ERR_INVALID_FRAME : XGL_OK;
    }

    if (ctx->auth_provider == NULL || ctx->auth_provider->sign == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    uint32_t key_id = 0U;
    uint8_t tag_len = 0U;
    xgl_error_t err = network_find_security_ext(frame_buf, header, &key_id, &tag_len);
    if (err != XGL_OK || tag_len == 0U || tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (ctx->auth_required && key_id != ctx->auth_key_id) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t tag_offset = (size_t)header->header_len + (size_t)header->payload_len;
    if (frame_len < tag_offset + (size_t)tag_len + XGL_CRC16_SIZE ||
        tag_offset + (size_t)tag_len != frame_len - XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    uint8_t tag[XGL_AUTH_TAG_MAX_LEN] = {0};
    size_t produced_tag_len = 0U;
    err = ctx->auth_provider->sign(key_id,
                                   frame_buf,
                                   header->header_len,
                                   &frame_buf[header->header_len],
                                   header->payload_len,
                                   tag,
                                   sizeof(tag),
                                   &produced_tag_len,
                                   ctx->auth_provider->user_data);
    if (err != XGL_OK) {
        return err;
    }

    if (produced_tag_len != tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    memcpy(&frame_buf[tag_offset], tag, produced_tag_len);
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
    ctx->auth_required = config->auth_required;
    ctx->auth_key_id = config->auth_key_id;
    ctx->auth_provider = config->auth_provider;

    return XGL_OK;
}

/**
 * \brief           Send packet through network layer
 * \details         Performs route lookup and forwards packet to data link layer
 */
static xgl_error_t xgl_network_send_with_handle(xgl_network_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                xgl_packet_t* packet,
                                                bool assign_packet_number) {
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
                "Route not found for target ID: %u", (unsigned int)packet->target_id);

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

    /* Packet number assignment is handled by transport layer. */
    (void)assign_packet_number;  /* Unused in this layer */

    /* Update statistics */
    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        if (packet->data != NULL) {
            ctx->stats->tx_bytes += packet->data->data_len;
        }
    }

    /* Build frame from packet for datalink transmission */
    xgl_frame_t frame;
    uint8_t extensions[UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE] = {0};
    size_t extensions_len = 0U;
    xgl_error_t err = network_copy_packet_extensions(extensions,
                                                     sizeof(extensions),
                                                     &extensions_len,
                                                     packet);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return err;
    }

    uint8_t reliability_class = XGL_RELIABILITY_NONE;
    if (packet->reliable == XGL_RELIABILITY_ACK_ONLY) {
        reliability_class = XGL_RELIABILITY_ACK_ONLY;
    } else if (packet->reliable != 0U) {
        reliability_class = XGL_RELIABILITY_ACK_ELICITING;
    }

    xgl_frame_params_t params = {
        .source_id = packet->source_id,
        .target_id = packet->target_id,
        .data_type = packet->data_type,
        .packet_type = packet->packet_type,
        .flags = packet->flags,
        .traffic_class = packet->traffic_class,
        .connection_id = packet->connection_id,
        .packet_number = packet->packet_number,
        .session_epoch = packet->session_epoch,
        .extensions = (extensions_len > 0U) ? extensions : NULL,
        .extensions_len = extensions_len,
        .payload = packet->data->data,
        .payload_len = packet->data->data_len,
        .reliable = packet->reliable,
        .reliability_class = reliability_class,
        .fragment = packet->fragment,
        .priority = packet->priority,
        .session_id = packet->session_id,
        .ttl = XGL_DEFAULT_TTL
    };

    err = xgl_frame_build(&frame, &params);

    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        return err;
    }

    size_t auth_tag_len = 0U;
    if (ctx->auth_required) {
        if (ctx->auth_provider == NULL ||
            ctx->auth_provider->sign == NULL ||
            ctx->auth_provider->tag_len == 0U ||
            ctx->auth_provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
            if (ctx->stats != NULL) {
                ctx->stats->tx_errors++;
            }
            return XGL_ERR_INVALID_PARAM;
        }
        auth_tag_len = ctx->auth_provider->tag_len;
    }
    if (xgl_frame_serialized_size(frame.payload_len,
                                  frame.extensions_len,
                                  auth_tag_len) > route->max_frame_size) {
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
                             bool assign_packet_number) {
    return xgl_network_send_with_handle(ctx, NULL, packet, assign_packet_number);
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
    uint16_t source_id, target_id;
    uint8_t data_type;
    uint32_t session_epoch;
    const uint8_t* payload;
    size_t payload_len;

    xgl_error_t err = xgl_network_extract_packet_info(frame_buf, frame_len,
                                                      &source_id, &target_id,
                                                      &data_type,
                                                      &session_epoch,
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
            xgl_wire_header_t wire_header;
            if (xgl_wire_decode_header(&wire_header, frame_buf, frame_len) != XGL_OK) {
                return XGL_ERR_INVALID_FRAME;
            }
            uint8_t reliable = XGL_RELIABILITY_NONE;
            uint8_t traffic_reliability =
                (uint8_t)(wire_header.traffic_class & XGL_RELIABILITY_CLASS_MASK);
            if (traffic_reliability == XGL_RELIABILITY_ACK_ELICITING ||
                (wire_header.flags & XGL_WIRE_FLAG_ACK_ELICITING) != 0U) {
                reliable = XGL_RELIABILITY_ACK_ELICITING;
            } else if (traffic_reliability == XGL_RELIABILITY_ACK_ONLY ||
                       wire_header.packet_type == XGL_PACKET_TYPE_ACK) {
                reliable = XGL_RELIABILITY_ACK_ONLY;
            }

            /* Build packet data structure for payload */
            xgl_packet_data_t packet_data = {
                .ref_count = 1,
                .data_len = payload_len,
                .data = payload,
                .owned_data = NULL
            };

            const uint8_t* extensions = NULL;
            size_t extensions_len = 0;
            if (wire_header.header_len > XGL_WIRE_BASE_HEADER_SIZE) {
                extensions = frame_buf + XGL_WIRE_BASE_HEADER_SIZE;
                extensions_len = wire_header.header_len - XGL_WIRE_BASE_HEADER_SIZE;
            }
            /* Build complete packet structure for transport layer */
            xgl_packet_t packet = {
                .source_id = source_id,
                .target_id = target_id,
                .session_id = (uint16_t)(wire_header.connection_id & UINT16_MAX),
                .connection_id = wire_header.connection_id,
                .packet_number = wire_header.packet_number,
                .session_epoch = session_epoch,
                .packet_type = wire_header.packet_type,
                .flags = wire_header.flags,
                .data_type = data_type,
                .reliable = reliable,
                .fragment = ((wire_header.flags & XGL_WIRE_FLAG_FRAGMENTED) != 0U) ? 1U : 0U,
                .priority = (wire_header.traffic_class & XGL_TRAFFIC_PRIORITY_MASK) >> XGL_TRAFFIC_PRIORITY_SHIFT,
                .ttl = wire_header.ttl,
                .traffic_class = wire_header.traffic_class,
                .data = &packet_data,
                .extensions = extensions,
                .extensions_len = extensions_len,
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
                    "No route for forwarding to target ID: %u", (unsigned int)target_id);

            if (ctx->error_callback != NULL) {
                ctx->error_callback(handle, XGL_ERR_ROUTE_NOT_FOUND,
                                  error_msg, ctx->callback_user_data);
            }

            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }

            return XGL_ERR_ROUTE_NOT_FOUND;
        }

        xgl_wire_header_t incoming_header;
        if (xgl_wire_decode_header(&incoming_header, frame_buf, frame_len) != XGL_OK) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_INVALID_FRAME;
        }

        if (incoming_header.ttl <= 1U) {
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

        if (frame_len > route->max_frame_size) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_BUFFER_TOO_SMALL;
        }

        uint8_t forward_buf[XGL_DATALINK_MAX_FRAME_SIZE];
        memcpy(forward_buf, frame_buf, frame_len);
        xgl_wire_header_t wire_header = incoming_header;
        wire_header.ttl = (uint8_t)(wire_header.ttl - 1U);
        if (xgl_wire_encode_header(forward_buf, frame_len, &wire_header) != XGL_OK) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_INVALID_FRAME;
        }

        if (network_resign_forwarded_frame(ctx,
                                           forward_buf,
                                           frame_len,
                                           &wire_header) != XGL_OK) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_INVALID_FRAME;
        }

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
                                  uint16_t target_id,
                                  uint16_t source_id) {
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

    if (source_id == target_id && target_id != XGL_BROADCAST_ID) {
        return false;
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
                                       // cppcheck-suppress constParameterCallback
                                       void* data) {
    xgl_network_ctx_t* net_ctx = (xgl_network_ctx_t*)ctx;

    if (net_ctx == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Extract frame buffer and length from data */
    const xgl_frame_rx_message_t* frame_data = (const xgl_frame_rx_message_t*)data;

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
