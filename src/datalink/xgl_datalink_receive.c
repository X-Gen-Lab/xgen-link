/**
 * \file            xgl_datalink_receive.c
 * \brief           Data link layer receive path
 * \author          X-Gen Lab
 */

#include <datalink/xgl_datalink.h>
#include <datalink/xgl_datalink_metadata.h>
#include <internal/xgl_diagnostics.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl_config.h>
#include <xgl/xgl_error.h>

/** \brief           Report one decode failure consistently for both ingresses.
 */
static void datalink_report_decode_error(xgl_datalink_ctx_t* ctx,
                                         xgl_wire_decode_status_t status) {
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_errors++;
    }
#endif
    const char* message = NULL;
    if (status == XGL_WIRE_DECODE_HEADER_CRC) {
#if XGL_FEATURE_STATISTICS
        if (ctx->rx_header_crc_errors != NULL) {
            (*ctx->rx_header_crc_errors)++;
        }
#endif
        message = XGL_ERROR_MESSAGE("Header CRC16 validation failed");
    } else if (status == XGL_WIRE_DECODE_FRAME_CRC) {
#if XGL_FEATURE_STATISTICS
        if (ctx->rx_crc16_errors != NULL) {
            (*ctx->rx_crc16_errors)++;
        }
#endif
        message = XGL_ERROR_MESSAGE("Frame CRC16 validation failed");
    }
    if (message != NULL && ctx->error_callback != NULL) {
        ctx->error_callback(ctx->owner_handle, XGL_ERR_CRC_FAILED, message,
                            ctx->callback_user_data);
    }
}

/** \brief           Deliver one validated borrowed view without decoding again.
 */
static xgl_error_t datalink_deliver_view(xgl_datalink_ctx_t* ctx,
                                         const xgl_wire_frame_view_t* view) {
    if (view->frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
#if XGL_FEATURE_STATISTICS
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
#endif
        if (ctx->error_callback != NULL) {
            ctx->error_callback(
                ctx->owner_handle, XGL_ERR_INVALID_FRAME,
                XGL_ERROR_MESSAGE("Frame size exceeds maximum allowed"),
                ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_FRAME;
    }
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += view->frame_len;
    }
#endif
    if (ctx->upper_layer != NULL && ctx->upper_layer->receive != NULL) {
        xgl_frame_rx_message_t message = {
            .frame_buf = view->frame_buf,
            .frame_len = view->frame_len,
            .view = view,
        };
        return ctx->upper_layer->receive(ctx->upper_layer->ctx,
                                         ctx->owner_handle, &message);
    }
    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
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
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (ctx == NULL || parser == NULL || phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy->rx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (xgl_parser_check_timeout(parser, current_time_ms, timeout_ms)) {
        xgl_parser_reset(parser);
#if XGL_FEATURE_STATISTICS
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
#endif
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle, XGL_ERR_TIMEOUT,
                                XGL_ERROR_MESSAGE("Parser timeout"),
                                ctx->callback_user_data);
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

    xgl_wire_frame_view_t view;
    for (size_t i = 0; i < rx_len; i++) {
        xgl_wire_decode_status_t status;
        xgl_parse_result_t result = xgl_parser_feed_byte_view(
            parser, rx_buffer[i], current_time_ms, &view, &status);

        if (result == XGL_PARSE_RESULT_COMPLETE) {
            (void)datalink_deliver_view(ctx, &view);
            xgl_parser_reset(parser);

        } else if (result == XGL_PARSE_RESULT_ERROR) {
            datalink_report_decode_error(ctx, status);
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
        xgl_wire_decode_status_t status = XGL_WIRE_DECODE_INVALID;
        if (metadata.header_crc_failed) {
            status = XGL_WIRE_DECODE_HEADER_CRC;
        } else if (metadata.frame_crc_failed) {
            status = XGL_WIRE_DECODE_FRAME_CRC;
        }
        datalink_report_decode_error(ctx, status);
        if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE &&
            ctx->error_callback != NULL) {
            ctx->error_callback(
                ctx->owner_handle, XGL_ERR_INVALID_FRAME,
                XGL_ERROR_MESSAGE("Frame size exceeds maximum allowed"),
                ctx->callback_user_data);
        }
        return err;
    }

    return datalink_deliver_view(ctx, &metadata.frame);
}
