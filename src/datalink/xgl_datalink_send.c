/**
 * \file            xgl_datalink_send.c
 * \brief           Data link TX path implementation
 */

#include <xgl/internal/xgl_datalink.h>
#include <xgl/xgl_config.h>

#include <xgen/memory/allocator.h>

static void datalink_count_tx_error(xgl_datalink_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

static void datalink_report_tx_error(xgl_datalink_ctx_t* ctx, xgl_error_t err,
                                     const char* message) {
    if (ctx->error_callback != NULL) {
        ctx->error_callback(ctx->owner_handle, err, message,
                            ctx->callback_user_data);
    }
}

/**
 * \brief           Validate the configured TX authentication tag size
 * \param[in]       ctx: Datalink authentication configuration
 * \param[in]       frame: Frame authentication flag
 * \param[out]      auth_tag_len: Trailer length, or zero for an unauthenticated
 *                  TX
 * \return          XGL_OK on success, error code otherwise
 */
static xgl_error_t datalink_auth_tag_len(const xgl_datalink_ctx_t* ctx,
                                         const xgl_frame_t* frame,
                                         size_t* auth_tag_len) {
    *auth_tag_len = 0U;
#if XGL_FEATURE_AUTH
    if (!ctx->security.auth_required &&
        (frame->header.flags & XGL_WIRE_FLAG_AUTHENTICATED) == 0U) {
        return XGL_OK;
    }

    if (ctx->security.provider == NULL ||
        ctx->security.provider->sign == NULL ||
        ctx->security.provider->tag_len == 0U ||
        ctx->security.provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_PARAM;
    }

    *auth_tag_len = ctx->security.provider->tag_len;
#else
    (void)ctx;
    (void)frame;
#endif
    return XGL_OK;
}

/**
 * \brief           Serialize and synchronously transmit using a borrowed buffer
 * \param[in,out]   ctx: Datalink context owning the frame allocator
 * \param[in]       phy: Physical layer operations
 * \param[in]       frame: Frame to serialize
 * \return          XGL_OK on success, error code otherwise
 * \note            The PHY must finish reading the frame before tx returns.
 */
xgl_error_t xgl_datalink_send(xgl_datalink_ctx_t* ctx, xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame) {
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

    size_t auth_tag_len = 0U;
    xgl_error_t err = datalink_auth_tag_len(ctx, frame, &auth_tag_len);
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        return err;
    }

    size_t frame_size = xgl_frame_serialized_size(
        frame->payload_len, frame->extensions_len, auth_tag_len);

    uint8_t* frame_buffer = (uint8_t*)xgm_alloc(ctx->allocator, frame_size);
    if (frame_buffer == NULL) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(ctx, XGL_ERR_NO_MEMORY,
                                 "Failed to allocate frame buffer");
        return XGL_ERR_NO_MEMORY;
    }

    size_t bytes_written = 0U;
#if XGL_FEATURE_AUTH
    if (auth_tag_len != 0U) {
        err = xgl_frame_serialize_authenticated(frame_buffer, frame_size, frame,
                                                &ctx->security, &bytes_written);
    } else
#endif
    {
        err = xgl_frame_serialize(frame_buffer, frame_size, frame,
                                  &bytes_written);
    }
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(ctx, err, "Frame serialization failed");
        xgm_free(ctx->allocator, frame_buffer);
        return err;
    }

    err = xgl_datalink_send_raw(ctx, phy, frame_buffer, bytes_written);
    xgm_free(ctx->allocator, frame_buffer);
    return err;
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

    xgl_error_t err = phy->tx(frame_buffer, frame_len, phy->user_data);
    if (err != XGL_OK) {
        datalink_count_tx_error(ctx);
        datalink_report_tx_error(ctx, err,
                                 "Physical layer transmission failed");
        return err;
    }

    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += frame_len;
    }

    return XGL_OK;
}
