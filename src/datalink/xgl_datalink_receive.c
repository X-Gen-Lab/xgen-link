/**
 * \file            xgl_datalink_receive.c
 * \brief           Data link layer receive path
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_datalink_metadata.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_config.h>
#include <xgl/xgl_error.h>

/**
 * \brief           Poll a PHY through the compatibility context parser
 * \param[in,out]   ctx: Shared datalink context
 * \param[in]       phy: Physical layer operations
 * \param[in]       current_time_ms: Current time in milliseconds
 * \param[in]       timeout_ms: Parser timeout in milliseconds
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_receive(xgl_datalink_ctx_t* ctx, xgl_phy_ops_t* phy,
                                 uint32_t current_time_ms,
                                 uint32_t timeout_ms) {
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return xgl_datalink_poll_parser(ctx, &ctx->parser, phy, current_time_ms,
                                    timeout_ms, XGL_DATALINK_RX_CHUNK_SIZE);
}

/**
 * \brief           Receive a bounded byte chunk with the selected link parser
 * \param[in,out]   ctx: Shared datalink policy and delivery context
 * \param[in,out]   parser: Parser owned by this PHY
 * \param[in]       phy: Physical layer operations
 * \param[in]       current_time_ms: Current time in milliseconds
 * \param[in]       timeout_ms: Parser timeout in milliseconds
 * \param[in]       byte_budget: Maximum bytes to read in this call
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_poll_parser(xgl_datalink_ctx_t* ctx,
                                     xgl_parser_t* parser, xgl_phy_ops_t* phy,
                                     uint32_t current_time_ms,
                                     uint32_t timeout_ms, size_t byte_budget) {
    if (ctx == NULL || parser == NULL || phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy->rx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (xgl_parser_check_timeout(parser, current_time_ms, timeout_ms)) {
        xgl_parser_reset(parser);
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, XGL_ERR_TIMEOUT,
                                "Parser timeout", ctx->callback_user_data);
        }
    }

    uint8_t rx_buffer[XGL_DATALINK_RX_CHUNK_SIZE];
    size_t rx_capacity =
        byte_budget < sizeof(rx_buffer) ? byte_budget : sizeof(rx_buffer);
    if (rx_capacity == 0U) {
        return XGL_OK;
    }
    size_t rx_len = rx_capacity;

    xgl_error_t err = phy->rx(rx_buffer, &rx_len, phy->user_data);
    if (err != XGL_OK) {
        return err;
    }
    if (rx_len > rx_capacity) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (rx_len == 0) {
        return XGL_OK;
    }

    for (size_t i = 0; i < rx_len; i++) {
        xgl_parse_result_t result =
            xgl_parser_feed_byte(parser, rx_buffer[i], current_time_ms);

        if (result == XGL_PARSE_RESULT_COMPLETE) {
            uint8_t* frame_buffer = NULL;
            size_t frame_len = 0;

            err = xgl_parser_get_frame(parser, &frame_buffer, &frame_len);
            if (err == XGL_OK) {
                xgl_datalink_process_frame(ctx, frame_buffer, frame_len);
            }

            xgl_parser_reset(parser);

        } else if (result == XGL_PARSE_RESULT_ERROR) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
            }
        }
    }

    return XGL_OK;
}

/**
 * \brief           Validate frame policy and deliver a borrowed view upstream
 * \param[in,out]   ctx: Shared security, statistics and delivery context
 * \param[in]       frame_buffer: Complete frame bytes
 * \param[in]       frame_len: Available bytes including the final CRC
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_process_frame(xgl_datalink_ctx_t* ctx,
                                       const uint8_t* frame_buffer,
                                       size_t frame_len) {
    if (ctx == NULL || frame_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_datalink_rx_metadata_t metadata;
    xgl_error_t err =
        xgl_datalink_decode_rx_metadata(frame_buffer, frame_len, &metadata);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        if (metadata.header_crc_failed && ctx->rx_header_crc_errors != NULL) {
            (*ctx->rx_header_crc_errors)++;
        }
        if (metadata.frame_crc_failed) {
            if (ctx->rx_crc16_errors != NULL) {
                (*ctx->rx_crc16_errors)++;
            }
            if (ctx->error_callback != NULL) {
                ctx->error_callback(ctx->owner_handle, XGL_ERR_CRC_FAILED,
                                    "Frame CRC16 validation failed",
                                    ctx->callback_user_data);
            }
        }
        if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE &&
            ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, XGL_ERR_INVALID_FRAME,
                                "Frame size exceeds maximum allowed",
                                ctx->callback_user_data);
        }
        return err;
    }

    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += frame_len;
    }

    if (ctx->upper_layer != NULL && ctx->upper_layer->receive != NULL) {
        xgl_frame_rx_message_t frame_data = {.frame_buf = frame_buffer,
                                             .frame_len = frame_len,
                                             .view = &metadata.frame};

        return ctx->upper_layer->receive(ctx->upper_layer->ctx,
                                         ctx->owner_handle, &frame_data);
    }

    return XGL_OK;
}
