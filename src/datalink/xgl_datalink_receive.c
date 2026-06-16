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

static xgl_replay_result_t datalink_check_replay(xgl_datalink_ctx_t* ctx,
                                                 uint16_t source_id,
                                                 uint32_t connection_id,
                                                 uint32_t session_epoch,
                                                 uint32_t packet_number) {
    if (ctx == NULL) {
        return XGL_REPLAY_REJECT;
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
            return xgl_replay_window_check(window,
                                           source_id,
                                           connection_id,
                                           session_epoch,
                                           packet_number);
        }
    }

    if (free_index == XGL_DATALINK_REPLAY_WINDOW_COUNT) {
        return XGL_REPLAY_REJECT;
    }

    if (xgl_replay_window_init(&ctx->replay_windows[free_index],
                               source_id,
                               connection_id,
                               session_epoch,
                               XGL_DATALINK_REPLAY_WINDOW_SIZE) != XGL_OK) {
        return XGL_REPLAY_REJECT;
    }
    ctx->replay_window_used[free_index] = true;
    return xgl_replay_window_check(&ctx->replay_windows[free_index],
                                   source_id,
                                   connection_id,
                                   session_epoch,
                                   packet_number);
}

static bool datalink_is_ack_eliciting(const xgl_wire_header_t* wire_header) {
    if (wire_header == NULL) {
        return false;
    }

    uint8_t traffic_reliability =
        (uint8_t)(wire_header->traffic_class & XGL_RELIABILITY_CLASS_MASK);
    return traffic_reliability == XGL_RELIABILITY_ACK_ELICITING ||
           (wire_header->flags & XGL_WIRE_FLAG_ACK_ELICITING) != 0U;
}

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

    if (xgl_parser_check_timeout(&ctx->parser, current_time_ms, timeout_ms)) {
        xgl_parser_reset(&ctx->parser);
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
        }
        if (ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle,
                                XGL_ERR_TIMEOUT,
                                "Parser timeout",
                                ctx->callback_user_data);
        }
    }

    uint8_t rx_buffer[XGL_DATALINK_RX_CHUNK_SIZE];
    size_t rx_len = sizeof(rx_buffer);

    xgl_error_t err = phy->rx(rx_buffer, &rx_len, phy->user_data);
    if (err != XGL_OK) {
        return err;
    }

    if (rx_len == 0) {
        return XGL_OK;
    }

    for (size_t i = 0; i < rx_len; i++) {
        xgl_parse_result_t result = xgl_parser_feed_byte(&ctx->parser,
                                                         rx_buffer[i],
                                                         current_time_ms);

        if (result == XGL_PARSE_RESULT_COMPLETE) {
            uint8_t* frame_buffer = NULL;
            size_t frame_len = 0;

            err = xgl_parser_get_frame(&ctx->parser, &frame_buffer, &frame_len);
            if (err == XGL_OK) {
                xgl_datalink_process_frame(ctx, frame_buffer, frame_len);
            }

            xgl_parser_reset(&ctx->parser);

        } else if (result == XGL_PARSE_RESULT_ERROR) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
            }
        }
    }

    return XGL_OK;
}

xgl_error_t xgl_datalink_process_frame(xgl_datalink_ctx_t* ctx,
                                       const uint8_t* frame_buffer,
                                       size_t frame_len) {
    if (ctx == NULL || frame_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_datalink_rx_metadata_t metadata;
    xgl_error_t err = xgl_datalink_decode_rx_metadata(frame_buffer,
                                                      frame_len,
                                                      ctx->auth_required,
                                                      ctx->auth_key_id,
                                                      &metadata);
    if (err != XGL_OK) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
            if (metadata.auth_key_rejected) {
                ctx->stats->rx_dropped++;
            }
        }
        if (metadata.header_crc_failed && ctx->rx_header_crc_errors != NULL) {
            (*ctx->rx_header_crc_errors)++;
        }
        if (metadata.frame_crc_failed) {
            if (ctx->rx_crc16_errors != NULL) {
                (*ctx->rx_crc16_errors)++;
            }
            if (ctx->error_callback != NULL) {
                ctx->error_callback(ctx->owner_handle,
                                    XGL_ERR_CRC_FAILED,
                                    "Frame CRC16 validation failed",
                                    ctx->callback_user_data);
            }
        }
        if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE && ctx->error_callback != NULL) {
            ctx->error_callback(ctx->owner_handle,
                                XGL_ERR_INVALID_FRAME,
                                "Frame size exceeds maximum allowed",
                                ctx->callback_user_data);
        }
        return err;
    }

    if ((ctx->auth_required || metadata.should_verify_auth) &&
        (!metadata.authenticated ||
         !metadata.has_security_ext ||
         ctx->auth_provider == NULL ||
         ctx->auth_provider->verify == NULL)) {
        if (ctx->stats != NULL) {
            ctx->stats->rx_errors++;
            ctx->stats->rx_dropped++;
        }
        return XGL_ERR_INVALID_FRAME;
    }

    if (metadata.should_verify_auth) {
        bool auth_valid = false;
        xgl_error_t auth_err = xgl_wire_verify_auth_trailer(frame_buffer,
                                                            frame_len - XGL_CRC16_SIZE,
                                                            metadata.header.header_len,
                                                            metadata.payload_len,
                                                            metadata.auth_key_id,
                                                            ctx->auth_provider,
                                                            &auth_valid);
        if (auth_err != XGL_OK || !auth_valid) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_INVALID_FRAME;
        }

        xgl_replay_result_t replay_result =
            datalink_check_replay(ctx,
                                  metadata.header.source_id,
                                  metadata.header.connection_id,
                                  metadata.session_epoch,
                                  metadata.header.packet_number);
        if (replay_result == XGL_REPLAY_REJECT ||
            (replay_result == XGL_REPLAY_ACCEPT_DUPLICATE &&
             !datalink_is_ack_eliciting(&metadata.header))) {
            if (ctx->stats != NULL) {
                ctx->stats->rx_errors++;
                ctx->stats->rx_dropped++;
            }
            return XGL_ERR_INVALID_FRAME;
        }
    }

    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += frame_len;
    }

    if (ctx->upper_layer != NULL && ctx->upper_layer->receive != NULL) {
        xgl_frame_rx_message_t frame_data = {
            .frame_buf = frame_buffer,
            .frame_len = frame_len
        };

        (void)ctx->upper_layer->receive(ctx->upper_layer->ctx,
                                        ctx->owner_handle,
                                        &frame_data);
    }

    return XGL_OK;
}
