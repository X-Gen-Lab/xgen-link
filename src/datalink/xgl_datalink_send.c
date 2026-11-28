/**
 * \file            xgl_datalink_send.c
 * \brief           Data link TX path implementation
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-16
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <datalink/xgl_datalink.h>
#include <internal/xgl_diagnostics.h>
#include <wire/xgl_frame_internal.h>
#include <xgl/xgl_config.h>

#include "internal/xgl_allocator.h"

static void datalink_count_tx_error(xgl_datalink_ctx_t* ctx) {
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
#else
    (void)ctx;
#endif
}

static void datalink_report_tx_error(xgl_datalink_ctx_t* ctx, xgl_error_t err,
                                     const char* message) {
    (void)message;
    if (ctx->error_callback != NULL) {
        ctx->error_callback(ctx->owner_handle, err, XGL_ERROR_MESSAGE(message),
                            ctx->callback_user_data);
    }
}

/**
 * \brief           Validate the configured TX authentication tag size
 * \details         An unauthenticated frame has a zero-length authentication
 *                  trailer.
 */
static xgl_error_t datalink_auth_tag_len(const xgl_datalink_ctx_t* ctx,
                                         const xgl_frame_t* frame,
                                         size_t* auth_tag_len) {
    *auth_tag_len = 0U;
#if XGL_FEATURE_AUTH
    const xgl_security_ctx_t* security = ctx->security;
    return xgl_security_frame_tag_len(
        security != NULL && security->auth_required,
        security != NULL ? security->provider : NULL, frame->header.flags,
        auth_tag_len);
#else
    (void)ctx;
    (void)frame;
    return XGL_OK;
#endif
}

/* Capacity is a byte count; payload_offset is an offset in the same buffer. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Serialize and synchronously transmit using a borrowed buffer
 * \details         A supplied layout has already been validated. With no
 *                  layout, measure it here. With no caller buffer, use bounded
 *                  scratch. The payload offset refers to the supplied buffer.
 * \note            The PHY must finish reading the frame before tx returns.
 */
static xgl_error_t datalink_encode_submit(xgl_datalink_ctx_t* ctx,
                                          xgl_phy_ops_t* phy,
                                          const xgl_frame_t* frame,
                                          const xgl_frame_layout_t* layout,
                                          uint8_t* buffer, size_t capacity,
                                          size_t payload_offset) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (ctx == NULL || phy == NULL || frame == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy->tx == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
#if !XGL_FEATURE_AUTH
    if ((frame->header.flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U) {
        return XGL_ERR_INVALID_FRAME;
    }
#endif

    xgl_frame_layout_t measured;
    if (layout == NULL) {
        size_t auth_tag_len;
        xgl_error_t error = datalink_auth_tag_len(ctx, frame, &auth_tag_len);
        if (error == XGL_OK) {
            error = xgl_frame_measure(frame, auth_tag_len, &measured);
        }
        if (error != XGL_OK) {
            datalink_count_tx_error(ctx);
            return error;
        }
        layout = &measured;
    }
    if (layout->frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
        datalink_count_tx_error(ctx);
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    const bool owned = buffer == NULL;
    if (!owned && capacity < layout->frame_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (!owned && (payload_offset != layout->header_len ||
                   frame->payload != buffer + payload_offset)) {
        return XGL_ERR_INVALID_PARAM;
    }
    const size_t frame_size = owned ? layout->frame_len : capacity;
    uint8_t* frame_buffer =
        owned ? xgl_alloc(ctx->allocator, frame_size) : buffer;
    if (frame_buffer == NULL) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(
            ctx, XGL_ERR_NO_MEMORY,
            XGL_ERROR_MESSAGE("Failed to allocate frame buffer"));
        return XGL_ERR_NO_MEMORY;
    }

    size_t bytes_written = 0U;
    xgl_error_t err;
#if XGL_FEATURE_AUTH
    if (layout->tag_len != 0U) {
        err = xgl_security_serialize_measured(frame_buffer, frame, layout,
                                              ctx->security, &bytes_written);
    } else
#endif
    {
        err = xgl_frame_encode_measured(frame_buffer, frame, layout);
        if (err == XGL_OK) {
            err = xgl_frame_finalize_crc(frame_buffer, frame_size, layout,
                                         &bytes_written);
        }
    }
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(
            ctx, err, XGL_ERROR_MESSAGE("Frame serialization failed"));
        if (owned) {
            xgl_free(ctx->allocator, frame_buffer);
        }
        return err;
    }

    err = xgl_datalink_send_raw(ctx, phy, frame_buffer, bytes_written);
    if (owned) {
        xgl_free(ctx->allocator, frame_buffer);
    }
    return err;
}

/** \brief           Encode using the bounded datalink scratch service. */
xgl_error_t xgl_datalink_send(xgl_datalink_ctx_t* ctx, xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame) {
    return datalink_encode_submit(ctx, phy, frame, NULL, NULL, 0U, 0U);
}

/** \brief           Use caller storage without allocation or payload movement.
 */
xgl_error_t xgl_datalink_send_inplace(xgl_datalink_ctx_t* ctx,
                                      xgl_phy_ops_t* phy,
                                      const xgl_frame_t* frame, uint8_t* buffer,
                                      size_t buffer_size,
                                      size_t payload_offset) {
    if (buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return datalink_encode_submit(ctx, phy, frame, NULL, buffer, buffer_size,
                                  payload_offset);
}

xgl_error_t xgl_datalink_submit(xgl_datalink_ctx_t* ctx,
                                const xgl_frame_tx_message_t* message) {
    if (message->frame == NULL) {
        return xgl_datalink_send_raw(ctx, message->phy, message->serialized,
                                     message->serialized_len);
    }
    return datalink_encode_submit(
        ctx, message->phy, message->frame, message->layout, message->buffer,
        message->buffer_size, message->payload_offset);
}

xgl_error_t xgl_datalink_send_raw(xgl_datalink_ctx_t* ctx, xgl_phy_ops_t* phy,
                                  const uint8_t* frame_buffer,
                                  size_t frame_len) {
    if (ctx == NULL || phy == NULL || frame_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy->tx == NULL || frame_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
        datalink_count_tx_error(ctx);
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_error_t err = phy->tx(frame_buffer, frame_len, phy->user_data);
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(
            ctx, err, XGL_ERROR_MESSAGE("Physical layer transmission failed"));
        return err;
    }

#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += frame_len;
    }
#endif

    return XGL_OK;
}
