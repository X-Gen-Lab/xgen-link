/**
 * \file            xgl_datalink_send.c
 * \brief           Data link TX path implementation
 */

#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_allocator.h>
#include <xgl/xgl_config.h>

static void datalink_count_tx_error(xgl_datalink_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

static void datalink_report_tx_error(xgl_datalink_ctx_t* ctx,
                                     xgl_error_t err,
                                     const char* message) {
    if (ctx->error_callback != NULL) {
        ctx->error_callback(ctx->owner_handle,
                            err,
                            message,
                            ctx->callback_user_data);
    }
}

static xgl_error_t datalink_auth_tag_len(const xgl_datalink_ctx_t* ctx,
                                         size_t* auth_tag_len) {
    *auth_tag_len = 0U;
    if (!ctx->auth_required) {
        return XGL_OK;
    }

    if (ctx->auth_provider == NULL ||
        ctx->auth_provider->sign == NULL ||
        ctx->auth_provider->tag_len == 0U ||
        ctx->auth_provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_PARAM;
    }

    *auth_tag_len = ctx->auth_provider->tag_len;
    return XGL_OK;
}

xgl_error_t xgl_datalink_send(xgl_datalink_ctx_t* ctx,
                              xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame) {
    if (ctx == NULL || phy == NULL || frame == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy->tx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t auth_tag_len = 0U;
    xgl_error_t err = datalink_auth_tag_len(ctx, &auth_tag_len);
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        return err;
    }

    size_t frame_size = xgl_frame_serialized_size(frame->payload_len,
                                                  frame->extensions_len,
                                                  auth_tag_len);

    uint8_t stack_buffer[XGL_DATALINK_STACK_BUFFER_SIZE];
    uint8_t* frame_buffer = stack_buffer;
    bool use_heap = false;

    if (frame_size > sizeof(stack_buffer)) {
        frame_buffer = (uint8_t*)xgl_alloc(ctx->allocator, frame_size);
        if (frame_buffer == NULL) {
            datalink_count_tx_error(ctx);
            datalink_report_tx_error(ctx,
                                     XGL_ERR_NO_MEMORY,
                                     "Failed to allocate frame buffer");
            return XGL_ERR_NO_MEMORY;
        }
        use_heap = true;
    }

    size_t bytes_written = 0U;
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
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(ctx, err, "Frame serialization failed");
        if (use_heap) {
            xgl_free(ctx->allocator, frame_buffer);
        }
        return err;
    }

    err = phy->tx(frame_buffer, bytes_written, phy->user_data);
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(ctx, err, "Physical layer transmission failed");
        if (use_heap) {
            xgl_free(ctx->allocator, frame_buffer);
        }
        return err;
    }

    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += bytes_written;
    }

    if (use_heap) {
        xgl_free(ctx->allocator, frame_buffer);
    }

    return XGL_OK;
}

xgl_error_t xgl_datalink_send_raw(xgl_datalink_ctx_t* ctx,
                                  xgl_phy_ops_t* phy,
                                  const uint8_t* frame_buffer,
                                  size_t frame_len) {
    if (ctx == NULL || phy == NULL || frame_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy->tx == NULL || frame_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_error_t err = phy->tx(frame_buffer, frame_len, phy->user_data);
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(ctx,
                                 err,
                                 "Physical layer transmission failed");
        return err;
    }

    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += frame_len;
    }

    return XGL_OK;
}
