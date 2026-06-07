/**
 * \file            xgl_datalink.c
 * \brief           Data link layer implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_datalink.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_parser.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_config.h>
#include <xgl/xgl_allocator.h>
#include <xgl/xgl_wire.h>
#include <string.h>

static bool datalink_accept_replay(xgl_datalink_ctx_t* ctx,
                                   uint16_t source_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch,
                                   uint32_t packet_number) {
    if (ctx == NULL) {
        return false;
    }

    size_t free_index = XGL_DATALINK_REPLAY_WINDOW_COUNT;
    for (size_t i = 0; i < XGL_DATALINK_REPLAY_WINDOW_COUNT; ++i) {
        if (!ctx->replay_window_used[i]) {
            if (free_index == XGL_DATALINK_REPLAY_WINDOW_COUNT) {
                free_index = i;
            }
            continue;
        }

        xgl_replay_window_t* window = &ctx->replay_windows[i];
        if (window->source_id == source_id &&
            window->connection_id == connection_id &&
            window->session_epoch == session_epoch) {
            return xgl_replay_window_accept(window,
                                            source_id,
                                            connection_id,
                                            session_epoch,
                                            packet_number);
        }
    }

    if (free_index == XGL_DATALINK_REPLAY_WINDOW_COUNT) {
        return false;
    }

    if (xgl_replay_window_init(&ctx->replay_windows[free_index],
                               source_id,
                               connection_id,
                               session_epoch,
                               XGL_DATALINK_REPLAY_WINDOW_SIZE) != XGL_OK) {
        return false;
    }
    ctx->replay_window_used[free_index] = true;
    return xgl_replay_window_accept(&ctx->replay_windows[free_index],
                                    source_id,
                                    connection_id,
                                    session_epoch,
                                    packet_number);
}

/*---------------------------------------------------------------------------*/
/* Data Link Layer Initialization                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize data link layer context
 */
xgl_error_t xgl_datalink_init(xgl_datalink_ctx_t* ctx,
                              const xgl_datalink_config_t* config) {
    if (ctx == NULL || config == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (config->rx_cache == NULL || config->stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_datalink_ctx_t));
    ctx->rx_cache = config->rx_cache;
    ctx->rx_cache_size = config->rx_cache_size;
    ctx->stats = config->stats;
    ctx->rx_crc8_errors = config->rx_crc8_errors;
    ctx->rx_crc16_errors = config->rx_crc16_errors;
    ctx->source_id = config->source_id;
    ctx->upper_layer = config->upper_layer;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->owner_handle = config->owner_handle;
    ctx->allocator = config->allocator;
    ctx->auth_required = config->auth_required;
    ctx->auth_key_id = config->auth_key_id;
    ctx->auth_provider = config->auth_provider;
    
    /* Initialize parser */
    xgl_error_t err = xgl_parser_init(&ctx->parser, config->rx_cache, config->rx_cache_size);
    if (err != XGL_OK) {
        return err;
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Frame Transmission                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send frame via physical layer
 */
xgl_error_t xgl_datalink_send(xgl_datalink_ctx_t* ctx,
                              xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame) {
    if (ctx == NULL || phy == NULL || frame == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (phy->tx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Calculate required buffer size */
    size_t frame_size = xgl_frame_calculate_size(frame->payload_len) +
                        frame->extensions_len;
    if (ctx->auth_required) {
        if (ctx->auth_provider == NULL || ctx->auth_provider->sign == NULL) {
            if (ctx->stats != NULL) {
                ctx->stats->tx_errors++;
            }
            return XGL_ERR_INVALID_PARAM;
        }
        frame_size += XGL_WIRE_EXT_HEADER_SIZE + 13U + 32U;
    }
    
    /* Use stack buffer for small frames, heap for large frames */
    uint8_t stack_buffer[XGL_DATALINK_STACK_BUFFER_SIZE];
    uint8_t* frame_buffer = NULL;
    bool use_heap = false;
    
    if (frame_size <= sizeof(stack_buffer)) {
        frame_buffer = stack_buffer;
    } else {
        /* Allocate from heap for large frames */
        frame_buffer = (uint8_t*)xgl_alloc(ctx->allocator, frame_size);
        if (frame_buffer == NULL) {
            if (ctx->stats != NULL) {
                ctx->stats->tx_errors++;
            }
            if (ctx->error_callback != NULL) {
                ctx->error_callback(ctx->owner_handle, XGL_ERR_NO_MEMORY,
                              "Failed to allocate frame buffer", ctx->callback_user_data);
            }
            return XGL_ERR_NO_MEMORY;
        }
        use_heap = true;
    }
    
    size_t bytes_written = 0;
    
    /* Serialize frame to buffer */
    xgl_error_t err;
    if (ctx->auth_required) {
        err = xgl_frame_serialize_authenticated(frame_buffer,
                                                frame_size,
                                                frame,
                                                ctx->auth_key_id,
                                                ctx->auth_provider,
                                                &bytes_written);
    } else {
        err = xgl_frame_serialize(frame_buffer, frame_size, frame, &bytes_written);
    }
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, err, "Frame serialization failed", ctx->callback_user_data);
        }
        if (use_heap) {
            xgl_free(ctx->allocator, frame_buffer);
        }
        return err;
    }
    
    /* Transmit via physical layer */
    err = phy->tx(frame_buffer, bytes_written, phy->user_data);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, err, "Physical layer transmission failed", ctx->callback_user_data);
        }
        if (use_heap) {
            xgl_free(ctx->allocator, frame_buffer);
        }
        return err;
    }
    
    /* Update statistics */
    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += bytes_written;
    }
    
    /* Free heap buffer if used */
    if (use_heap) {
        xgl_free(ctx->allocator, frame_buffer);
    }
    
    return XGL_OK;
}

/**
 * \brief           Send raw frame buffer via physical layer
 */
xgl_error_t xgl_datalink_send_raw(xgl_datalink_ctx_t* ctx,
                                  xgl_phy_ops_t* phy,
                                  const uint8_t* frame_buffer,
                                  size_t frame_len) {
    if (ctx == NULL || phy == NULL || frame_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (phy->tx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    if (frame_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Transmit via physical layer */
    xgl_error_t err = phy->tx(frame_buffer, frame_len, phy->user_data);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->tx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, err, "Physical layer transmission failed", ctx->callback_user_data);
        }
        return err;
    }
    
    /* Update statistics */
    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += frame_len;
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Frame Reception                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Receive and parse frames from physical layer
 */
xgl_error_t xgl_datalink_receive(xgl_datalink_ctx_t* ctx,
                                 xgl_phy_ops_t* phy,
                                 uint32_t current_time_ms,
                                 uint32_t timeout_ms) {
    if (ctx == NULL || phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (phy->rx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Check for parser timeout */
    if (xgl_parser_check_timeout(&ctx->parser, current_time_ms, timeout_ms)) {
        /* Timeout occurred, reset parser */
        xgl_parser_reset(&ctx->parser);
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, XGL_ERR_TIMEOUT,
                              "Parser timeout", ctx->callback_user_data);
        }
    }
    
    /* Read data from physical layer */
    uint8_t rx_buffer[XGL_DATALINK_RX_CHUNK_SIZE];
    size_t rx_len = sizeof(rx_buffer);
    
    xgl_error_t err = phy->rx(rx_buffer, &rx_len, phy->user_data);
    if (err != XGL_OK) {
        /* No data available or error */
        return err;
    }
    
    if (rx_len == 0) {
        /* No data received */
        return XGL_OK;
    }
    
    /* Feed bytes to parser */
    for (size_t i = 0; i < rx_len; i++) {
        xgl_parse_result_t result = xgl_parser_feed_byte(&ctx->parser, 
                                                         rx_buffer[i], 
                                                         current_time_ms);
        
        if (result == XGL_PARSE_RESULT_COMPLETE) {
            /* Frame complete, process it */
            uint8_t* frame_buffer = NULL;
            size_t frame_len = 0;
            
            err = xgl_parser_get_frame(&ctx->parser, &frame_buffer, &frame_len);
            if (err == XGL_OK) {
                /* Process the complete frame */
                xgl_datalink_process_frame(ctx, frame_buffer, frame_len);
            }
            
            /* Reset parser for next frame */
            xgl_parser_reset(&ctx->parser);
            
        } else if (result == XGL_PARSE_RESULT_ERROR) {
            /* Parse error occurred */
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
            }
            /* Parser is already reset by feed_byte on error */
        }
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Frame Processing                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Process received frame
 */
xgl_error_t xgl_datalink_process_frame(xgl_datalink_ctx_t* ctx,
                                       const uint8_t* frame_buffer,
                                       size_t frame_len) {
    if (ctx == NULL || frame_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Minimum frame size: header + CRC16 */
    size_t min_frame_size = XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE;
    if (frame_len < min_frame_size) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Maximum frame size check - prevent buffer overflow attacks */
    if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, XGL_ERR_INVALID_FRAME,
                              "Frame size exceeds maximum allowed",
                              ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Verify production magic */
    if (frame_buffer[0] != XGL_WIRE_MAGIC_0 ||
        frame_buffer[1] != XGL_WIRE_MAGIC_1) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_wire_header_t wire_header;
    if (xgl_wire_decode_header(&wire_header, frame_buffer, frame_len) != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        if (ctx->rx_crc8_errors != NULL) {
            (*ctx->rx_crc8_errors)++;
        }
        return XGL_ERR_CRC_FAILED;
    }

    uint8_t auth_tag_len = 0U;
    bool has_security_ext = false;
    uint32_t session_epoch = 0U;
    if (wire_header.header_len > XGL_WIRE_BASE_HEADER_SIZE) {
        xgl_wire_ext_cursor_t cursor;
        xgl_error_t ext_err = xgl_wire_ext_cursor_init(
            &cursor,
            &frame_buffer[XGL_WIRE_BASE_HEADER_SIZE],
            wire_header.header_len - XGL_WIRE_BASE_HEADER_SIZE
        );
        if (ext_err != XGL_OK) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
            }
            return XGL_ERR_INVALID_FRAME;
        }

        xgl_wire_ext_t ext;
        while ((ext_err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
            if (ext.type == XGL_WIRE_EXT_SECURITY) {
                uint32_t key_id = 0;
                uint64_t nonce_id = 0;
                uint8_t tag_len = 0;
                if (xgl_wire_decode_security_ext_value(ext.value,
                                                       ext.len,
                                                       &key_id,
                                                       &nonce_id,
                                                       &tag_len) != XGL_OK ||
                    tag_len == 0U) {
                    if (ctx->stats != NULL) {
                        ctx->stats->rx_errors++;
                    }
                    return XGL_ERR_INVALID_FRAME;
                }
                (void)nonce_id;
                if (ctx->auth_required && key_id != ctx->auth_key_id) {
                    if (ctx->stats != NULL) {
                        ctx->stats->rx_errors++;
                        ctx->stats->rx_dropped++;
                    }
                    return XGL_ERR_INVALID_FRAME;
                }
                auth_tag_len = tag_len;
                has_security_ext = true;
            } else if (ext.type == XGL_WIRE_EXT_SESSION) {
                uint64_t incarnation_id = 0;
                if (xgl_wire_decode_session_ext_value(ext.value,
                                                      ext.len,
                                                      &session_epoch,
                                                      &incarnation_id) != XGL_OK) {
                    if (ctx->stats != NULL) {
                        ctx->stats->rx_errors++;
                    }
                    return XGL_ERR_INVALID_FRAME;
                }
                (void)incarnation_id;
            }
        }
        if (ext_err != XGL_ERR_NOT_FOUND) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
            }
            return XGL_ERR_INVALID_FRAME;
        }
    }

    if (ctx->auth_required &&
        ((wire_header.flags & XGL_WIRE_FLAG_AUTHENTICATED) == 0U ||
         !has_security_ext ||
         ctx->auth_provider == NULL ||
         ctx->auth_provider->verify == NULL)) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
            ctx->stats->rx_dropped++;
        }
        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Validate frame CRC16 */
    size_t crc_offset = frame_len - XGL_CRC16_SIZE;
    uint16_t calculated_crc = xgl_crc16_modbus(frame_buffer, crc_offset);
    uint16_t received_crc = xgl_deserialize_u16_le(&frame_buffer[crc_offset]);
    
    if (calculated_crc != received_crc) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        if (ctx->rx_crc16_errors != NULL) {
            (*ctx->rx_crc16_errors)++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, XGL_ERR_CRC_FAILED,
                              "Frame CRC16 validation failed", 
                              ctx->callback_user_data);
        }
        return XGL_ERR_CRC_FAILED;
    }
    
    size_t payload_len = wire_header.payload_len;
    size_t expected_frame_len = wire_header.header_len +
                                payload_len +
                                (size_t)auth_tag_len +
                                XGL_CRC16_SIZE;
    if (frame_len != expected_frame_len) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        return XGL_ERR_INVALID_FRAME;
    }
    
    if (ctx->auth_required) {
        bool auth_valid = false;
        xgl_error_t auth_err = xgl_wire_verify_auth_trailer(frame_buffer,
                                                            frame_len - XGL_CRC16_SIZE,
                                                            wire_header.header_len,
                                                            payload_len,
                                                            ctx->auth_key_id,
                                                            ctx->auth_provider,
                                                            &auth_valid);
        if (auth_err != XGL_OK || !auth_valid) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_INVALID_FRAME;
        }

        if (!datalink_accept_replay(ctx,
                                    wire_header.source_id,
                                    wire_header.connection_id,
                                    session_epoch,
                                    wire_header.packet_number)) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_INVALID_FRAME;
        }
    }
    
    /* Update statistics */
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += frame_len;
    }
    
    /* Forward to network layer via interface */
    if (ctx->upper_layer != NULL && ctx->upper_layer->receive != NULL) {
        /* Create a temporary packet structure to pass frame data */
        /* The frame buffer contains the complete frame with header and CRC */
        /* Network layer will extract the necessary information */
        
        /* For now, we pass the frame buffer directly through the interface */
        /* The network layer's receive function expects frame_buf and frame_len */
        /* We'll use a simple wrapper structure to pass both parameters */
        
        xgl_frame_rx_message_t frame_data = {
            .frame_buf = frame_buffer,
            .frame_len = frame_len
        };
        
        xgl_error_t err = ctx->upper_layer->receive(
            ctx->upper_layer->ctx,
            ctx->owner_handle,
            &frame_data
        );
        
        if (err != XGL_OK) {
            /* Network layer processing failed, but we already validated the frame */
            /* This is not a datalink error, so we don't return error */
        }
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Layer Interface Implementation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Datalink layer send implementation (called by upper layers)
 * \details         This function is called by network layer to send frames
 */
static xgl_error_t datalink_send_impl(void* ctx,
                                     xgl_handle_t handle,
                                     void* data) {
    xgl_datalink_ctx_t* dl_ctx = (xgl_datalink_ctx_t*)ctx;
    
    (void)handle;  /* Unused in this implementation */
    
    if (dl_ctx == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Extract frame and PHY from data */
    xgl_frame_tx_message_t* send_data = (xgl_frame_tx_message_t*)data;
    
    if (send_data->frame == NULL || send_data->phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Forward to datalink send function */
    return xgl_datalink_send(dl_ctx, send_data->phy, send_data->frame);
}

/**
 * \brief           Datalink layer receive implementation (for upper layers)
 * \details         This function is called by upper layers to receive frames
 *                  Currently not used as datalink pushes data up via callbacks
 */
static xgl_error_t datalink_receive_impl(void* ctx,
                                        xgl_handle_t handle,
                                        void* data) {
    (void)ctx;
    (void)handle;
    (void)data;
    /* Datalink pushes data up, doesn't pull */
    return XGL_ERR_INVALID_PARAM;
}

/**
 * \brief           Datalink layer error reporting implementation
 * \details         This function is called to report errors to upper layers
 */
static xgl_error_t datalink_report_error_impl(void* ctx,
                                              xgl_handle_t handle,
                                              void* data) {
    xgl_datalink_ctx_t* dl_ctx = (xgl_datalink_ctx_t*)ctx;
    xgl_layer_error_info_t* error_info = (xgl_layer_error_info_t*)data;
    
    if (dl_ctx == NULL || error_info == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Forward error to callback if available */
    if (dl_ctx->error_callback != NULL) {
        dl_ctx->error_callback(handle, error_info->error,
                              error_info->message,
                              dl_ctx->callback_user_data);
    }
    
    return XGL_OK;
}

/**
 * \brief           Get datalink layer interface
 * \details         Returns the layer interface for this datalink instance
 * \param[in]       ctx: Datalink layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_get_interface(xgl_datalink_ctx_t* ctx,
                                      xgl_layer_interface_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    xgl_layer_interface_init(iface,
                            ctx,
                            datalink_send_impl,
                            datalink_receive_impl,
                            datalink_report_error_impl);
    
    return XGL_OK;
}
