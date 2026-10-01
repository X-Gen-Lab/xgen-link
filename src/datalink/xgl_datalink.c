/**
 * \file            xgl_datalink.c
 * \brief           Data link layer implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_parser.h>
#include <xgl/xgl_error.h>

#include <string.h>

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

    if (XGL_FEATURE_STATISTICS && config->stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_datalink_ctx_t));
    ctx->stats = config->stats;
    ctx->rx_header_crc_errors = config->rx_header_crc_errors;
    ctx->rx_crc16_errors = config->rx_crc16_errors;
    ctx->source_id = config->source_id;
    ctx->upper_layer = config->upper_layer;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->owner_handle = config->owner_handle;
    ctx->allocator = config->allocator;
#if XGL_FEATURE_AUTH
    ctx->security = config->security;
#endif

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Layer Interface Implementation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Datalink layer send implementation (called by upper layers)
 * \details         This function is called by network layer to send frames
 */
static xgl_error_t datalink_send_impl(void* ctx, xgl_handle_t handle,
                                      const xgl_frame_tx_message_t* send_data) {
    xgl_datalink_ctx_t* dl_ctx = (xgl_datalink_ctx_t*)ctx;

    (void)handle; /* Unused in this implementation */

    if (dl_ctx == NULL || send_data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (send_data->phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (send_data->frame == NULL) {
        return xgl_datalink_send_raw(dl_ctx, send_data->phy,
                                     send_data->serialized,
                                     send_data->serialized_len);
    }

    if (send_data->buffer != NULL) {
        return xgl_datalink_send_inplace(
            dl_ctx, send_data->phy, send_data->frame, send_data->buffer,
            send_data->buffer_size, send_data->payload_offset);
    }

    /* Forward to datalink send function */
    return xgl_datalink_send(dl_ctx, send_data->phy, send_data->frame);
}

/**
 * \brief           Get datalink layer interface
 * \details         Returns the layer interface for this datalink instance
 * \param[in]       ctx: Datalink layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_get_interface(xgl_datalink_ctx_t* ctx,
                                       xgl_frame_interface_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_frame_interface_init(iface, ctx, datalink_send_impl, NULL);

    return XGL_OK;
}
