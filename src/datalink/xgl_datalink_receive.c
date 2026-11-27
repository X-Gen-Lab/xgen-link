/**
 * \file            xgl_datalink_receive.c
 * \brief           Data link layer receive path
 * \author          X-Gen Lab
 */

#include <datalink/xgl_datalink.h>
#include <internal/xgl_diagnostics.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl_config.h>
#include <xgl/xgl_error.h>

#if XGL_FEATURE_ADVANCED_RUNTIME
#include <string.h>
#endif

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

/** \brief           Apply the same frame-size policy before either delivery. */
static xgl_error_t datalink_check_frame_size(xgl_datalink_ctx_t* ctx,
                                             size_t frame_len) {
    if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
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
    return XGL_OK;
}

/** \brief           Deliver a validated view within the datalink size limit. */
static xgl_error_t datalink_deliver_view(xgl_datalink_ctx_t* ctx,
                                         const xgl_wire_frame_view_t* view,
                                         uint8_t* mutable_frame) {
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += view->frame_len;
    }
#endif
    if (ctx->upper_layer != NULL && ctx->upper_layer->receive != NULL) {
        xgl_frame_rx_message_t message = {
            .view = view,
            .mutable_frame = mutable_frame,
        };
        return ctx->upper_layer->receive(ctx->upper_layer->ctx,
                                         ctx->owner_handle, &message);
    }
    return XGL_OK;
}

/** \brief           Reset expired parser state once without reading the PHY. */
xgl_error_t xgl_datalink_expire_parser(xgl_datalink_ctx_t* ctx,
                                       xgl_parser_t* parser, uint32_t now_ms,
                                       uint32_t timeout_ms) {
    if (!xgl_parser_check_timeout(parser, now_ms, timeout_ms)) {
        return XGL_OK;
    }
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
    return XGL_ERR_TIMEOUT;
}

#if XGL_FEATURE_ADVANCED_RUNTIME
/* Span length and arrival time retain the synchronous ingress ordering. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_datalink_consume_span_bounded(xgl_datalink_ctx_t* ctx,
                                              xgl_parser_t* parser,
                                              const uint8_t* bytes, size_t len,
                                              uint32_t now_ms,
                                              size_t* candidates,
                                              size_t* consumed, bool* pending) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (ctx == NULL || parser == NULL || candidates == NULL ||
        consumed == NULL || pending == NULL || (bytes == NULL && len != 0U)) {
        return XGL_ERR_NULL_POINTER;
    }
    *consumed = 0U;
    *pending = false;
    xgl_error_t first_error = XGL_OK;
    for (;;) {
        xgl_wire_frame_view_t view;
        xgl_wire_decode_status_t status;
        xgl_parse_result_t result =
            xgl_parser_buffered_view(parser, candidates, &view, &status);
        if (result == XGL_PARSE_RESULT_PENDING) {
            *pending = true;
            return first_error;
        }
        if (result == XGL_PARSE_RESULT_COMPLETE) {
            xgl_error_t error = datalink_check_frame_size(ctx, view.frame_len);
            if (error == XGL_OK) {
                error = datalink_deliver_view(ctx, &view, parser->cache);
            }
            if (first_error == XGL_OK) {
                first_error = error;
            }
            continue;
        }
        if (result == XGL_PARSE_RESULT_ERROR) {
            datalink_report_decode_error(ctx, status);
            continue;
        }
        if (*consumed == len) {
            return first_error;
        }
        uint8_t* destination;
        size_t available = xgl_parser_input_space(parser, &destination);
        size_t append = len - *consumed;
        if (append > available) {
            append = available;
        }
        /* INCOMPLETE has consumed its prior batch and has room for more. */
        if (append == 0U) {
            return first_error == XGL_OK ? XGL_ERR_BUFFER_TOO_SMALL
                                         : first_error;
        }
        memmove(destination, bytes + *consumed, append);
        xgl_parser_commit_input(parser, append, now_ms);
        *consumed += append;
    }
}

/* Timeout and byte budget retain the existing PHY polling parameter order. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_datalink_poll_parser_bounded(
    xgl_datalink_ctx_t* ctx, xgl_parser_t* parser, xgl_phy_ops_t* phy,
    uint32_t now_ms, uint32_t timeout_ms, size_t byte_budget,
    size_t* candidates, bool* polled, bool* pending) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (ctx == NULL || parser == NULL || phy == NULL || candidates == NULL ||
        polled == NULL || pending == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *pending = false;
    *polled = false;
    if (phy->rx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    xgl_error_t first_error =
        xgl_datalink_expire_parser(ctx, parser, now_ms, timeout_ms);
    size_t consumed;
    xgl_error_t error = xgl_datalink_consume_span_bounded(
        ctx, parser, NULL, 0U, now_ms, candidates, &consumed, pending);
    if (first_error == XGL_OK) {
        first_error = error;
    }
    if (*pending || byte_budget == 0U) {
        return first_error;
    }
    uint8_t* destination;
    size_t capacity = xgl_parser_input_space(parser, &destination);
    if (capacity > byte_budget) {
        capacity = byte_budget;
    }
    if (capacity > XGL_DATALINK_RX_CHUNK_SIZE) {
        capacity = XGL_DATALINK_RX_CHUNK_SIZE;
    }
    if (capacity == 0U) {
        return first_error;
    }
    size_t received = capacity;
    *polled = true;
    error = phy->rx(destination, &received, phy->user_data);
    if (error != XGL_OK || received > capacity) {
        if (first_error == XGL_OK) {
            first_error = error != XGL_OK ? error : XGL_ERR_INVALID_PARAM;
        }
        return first_error;
    }
    xgl_parser_commit_input(parser, received, now_ms);
    error = xgl_datalink_consume_span_bounded(ctx, parser, NULL, 0U, now_ms,
                                              candidates, &consumed, pending);
    return first_error == XGL_OK ? error : first_error;
}
#endif

/* Span length and explicit time retain their documented parameter order. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_datalink_consume_span(xgl_datalink_ctx_t* ctx,
                                      xgl_parser_t* parser,
                                      const uint8_t* bytes, size_t len,
                                      uint32_t current_time_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
#if XGL_FEATURE_ADVANCED_RUNTIME
    size_t candidates = SIZE_MAX;
    size_t consumed;
    bool pending;
    return xgl_datalink_consume_span_bounded(ctx, parser, bytes, len,
                                             current_time_ms, &candidates,
                                             &consumed, &pending);
#else
    if (ctx == NULL || parser == NULL || (bytes == NULL && len != 0U)) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_error_t first_error = XGL_OK;
    xgl_wire_frame_view_t view;
    xgl_wire_decode_status_t status;
    for (;;) {
        xgl_parse_result_t result =
            xgl_parser_advance_view(parser, &view, &status);
        while (result == XGL_PARSE_RESULT_INCOMPLETE) {
            if (len == 0U) {
                return first_error;
            }
            result = xgl_parser_feed_byte_view(parser, *bytes++,
                                               current_time_ms, &view, &status);
            --len;
        }

        if (result == XGL_PARSE_RESULT_COMPLETE) {
            xgl_error_t error = datalink_check_frame_size(ctx, view.frame_len);
            if (error == XGL_OK) {
                error = datalink_deliver_view(ctx, &view, parser->cache);
            }
            if (first_error == XGL_OK) {
                first_error = error;
            }
        } else {
            datalink_report_decode_error(ctx, status);
        }
    }
#endif
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
#if XGL_FEATURE_ADVANCED_RUNTIME
    size_t candidates = SIZE_MAX;
    bool polled;
    bool pending;
    return xgl_datalink_poll_parser_bounded(ctx, parser, phy, current_time_ms,
                                            timeout_ms, byte_budget,
                                            &candidates, &polled, &pending);
#else
    if (ctx == NULL || parser == NULL || phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy->rx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_error_t first_error =
        xgl_datalink_expire_parser(ctx, parser, current_time_ms, timeout_ms);

    uint8_t rx_buffer[XGL_DATALINK_RX_CHUNK_SIZE];
    size_t rx_capacity =
        byte_budget < sizeof(rx_buffer) ? byte_budget : sizeof(rx_buffer);
    if (rx_capacity == 0U) {
        return first_error;
    }
    size_t rx_len = rx_capacity;

    xgl_error_t err = phy->rx(rx_buffer, &rx_len, phy->user_data);
    if (err != XGL_OK) {
        return first_error == XGL_OK ? err : first_error;
    }
    if (rx_len > rx_capacity) {
        return first_error == XGL_OK ? XGL_ERR_INVALID_PARAM : first_error;
    }

    err = xgl_datalink_consume_span(ctx, parser, rx_buffer, rx_len,
                                    current_time_ms);
    return first_error == XGL_OK ? err : first_error;
#endif
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

    xgl_error_t err = datalink_check_frame_size(ctx, frame_len);
    if (err != XGL_OK) {
        return err;
    }
    xgl_wire_frame_view_t view;
    xgl_wire_decode_status_t status;
    err = xgl_wire_decode_frame(&view, frame_buffer, frame_len, &status);
    if (err != XGL_OK) {
        datalink_report_decode_error(ctx, status);
        return err;
    }

    return datalink_deliver_view(ctx, &view, NULL);
}
