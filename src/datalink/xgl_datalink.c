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
#include <string.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
/* Data Link Layer Initialization                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize data link layer context
 */
xgl_error_t xgl_datalink_init(xgl_datalink_ctx_t* ctx,
                              uint8_t* rx_cache,
                              size_t rx_cache_size,
                              xgl_statistics_t* stats,
                              uint8_t source_id,
                              xgl_rx_callback_t rx_callback,
                              xgl_error_callback_t error_callback,
                              void* callback_user_data) {
    if (ctx == NULL || rx_cache == NULL || stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_datalink_ctx_t));
    ctx->rx_cache = rx_cache;
    ctx->rx_cache_size = rx_cache_size;
    ctx->stats = stats;
    ctx->source_id = source_id;
    ctx->rx_callback = rx_callback;
    ctx->error_callback = error_callback;
    ctx->callback_user_data = callback_user_data;
    
    /* Initialize parser */
    xgl_error_t err = xgl_parser_init(&ctx->parser, rx_cache, rx_cache_size);
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
xgl_error_t xgl_datalink_send(xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame,
                              xgl_statistics_t* stats,
                              xgl_error_callback_t error_callback,
                              void* callback_user_data) {
    if (phy == NULL || frame == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (phy->tx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Calculate required buffer size */
    size_t frame_size = xgl_frame_calculate_size(frame->payload_len);
    
    /* Use stack buffer for small frames, heap for large frames */
    uint8_t stack_buffer[256];
    uint8_t* frame_buffer = NULL;
    bool use_heap = false;
    
    if (frame_size <= sizeof(stack_buffer)) {
        frame_buffer = stack_buffer;
    } else {
        /* Allocate from heap for large frames */
        frame_buffer = (uint8_t*)malloc(frame_size);
        if (frame_buffer == NULL) {
            if (stats != NULL) {
                stats->tx_errors++;
            }
            if (error_callback != NULL) {
                error_callback(NULL, XGL_ERR_NO_MEMORY, 
                              "Failed to allocate frame buffer", callback_user_data);
            }
            return XGL_ERR_NO_MEMORY;
        }
        use_heap = true;
    }
    
    size_t bytes_written = 0;
    
    /* Serialize frame to buffer */
    xgl_error_t err = xgl_frame_serialize(frame_buffer, frame_size, frame, &bytes_written);
    if (err != XGL_OK) {
        if (stats != NULL) {
            stats->tx_errors++;
        }
        if (error_callback != NULL) {
            error_callback(NULL, err, "Frame serialization failed", callback_user_data);
        }
        if (use_heap) {
            free(frame_buffer);
        }
        return err;
    }
    
    /* Transmit via physical layer */
    err = phy->tx(frame_buffer, bytes_written, phy->user_data);
    if (err != XGL_OK) {
        if (stats != NULL) {
            stats->tx_errors++;
        }
        if (error_callback != NULL) {
            error_callback(NULL, err, "Physical layer transmission failed", callback_user_data);
        }
        if (use_heap) {
            free(frame_buffer);
        }
        return err;
    }
    
    /* Update statistics */
    if (stats != NULL) {
        stats->tx_packets++;
        stats->tx_bytes += bytes_written;
    }
    
    /* Free heap buffer if used */
    if (use_heap) {
        free(frame_buffer);
    }
    
    return XGL_OK;
}

/**
 * \brief           Send raw frame buffer via physical layer
 */
xgl_error_t xgl_datalink_send_raw(xgl_phy_ops_t* phy,
                                  const uint8_t* frame_buffer,
                                  size_t frame_len,
                                  xgl_statistics_t* stats,
                                  xgl_error_callback_t error_callback,
                                  void* callback_user_data) {
    if (phy == NULL || frame_buffer == NULL) {
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
        if (stats != NULL) {
            stats->tx_errors++;
        }
        if (error_callback != NULL) {
            error_callback(NULL, err, "Physical layer transmission failed", callback_user_data);
        }
        return err;
    }
    
    /* Update statistics */
    if (stats != NULL) {
        stats->tx_packets++;
        stats->tx_bytes += frame_len;
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
            ctx->error_callback(NULL, XGL_ERR_TIMEOUT, 
                              "Parser timeout", ctx->callback_user_data);
        }
    }
    
    /* Read data from physical layer */
    uint8_t rx_buffer[256];  /* Temporary RX buffer */
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
    
    /* Verify SOF */
    if (frame_buffer[0] != XGL_SOF) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Decode frame header */
    xgl_frame_header_t header;
    xgl_frame_decode_header(&header, frame_buffer);
    
    /* Validate header CRC8 */
    if (!xgl_frame_validate_header_crc(&header)) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_crc8_errors++;
            ctx->stats->rx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(NULL, XGL_ERR_CRC_FAILED, 
                              "Header CRC8 validation failed", 
                              ctx->callback_user_data);
        }
        return XGL_ERR_CRC_FAILED;
    }
    
    /* Validate frame CRC16 */
    size_t crc_offset = frame_len - XGL_CRC16_SIZE;
    uint16_t calculated_crc = xgl_crc16_modbus(frame_buffer, crc_offset);
    uint16_t received_crc = xgl_deserialize_u16_le(&frame_buffer[crc_offset]);
    
    if (calculated_crc != received_crc) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_crc16_errors++;
            ctx->stats->rx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(NULL, XGL_ERR_CRC_FAILED, 
                              "Frame CRC16 validation failed", 
                              ctx->callback_user_data);
        }
        return XGL_ERR_CRC_FAILED;
    }
    
    /* Extract payload */
    const uint8_t* payload = NULL;
    size_t payload_len = header.data_len;
    
    if (payload_len > 0) {
        payload = &frame_buffer[XGL_FRAME_HEADER_SIZE];
        
        /* Verify payload length matches frame size */
        size_t expected_frame_len = XGL_FRAME_HEADER_SIZE + payload_len + XGL_CRC16_SIZE;
        if (frame_len != expected_frame_len) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
            }
            return XGL_ERR_INVALID_FRAME;
        }
    }
    
    /* Update statistics */
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += frame_len;
    }
    
    /* Invoke receive callback if registered */
    if (ctx->rx_callback != NULL) {
        ctx->rx_callback(NULL, 
                        header.source_id, 
                        xgl_frame_get_datatype(&header), 
                        payload, 
                        payload_len, 
                        ctx->callback_user_data);
    }
    
    return XGL_OK;
}
