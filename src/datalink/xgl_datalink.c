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

    if (config->rx_cache == NULL || config->stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_datalink_ctx_t));
    ctx->rx_cache = config->rx_cache;
    ctx->rx_cache_size = config->rx_cache_size;
    ctx->stats = config->stats;
    ctx->rx_header_crc_errors = config->rx_header_crc_errors;
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
 * \brief           Reject pull-style receive calls
 * \details         The datalink layer receives from PHY and pushes parsed frames upward.
 */
static xgl_error_t datalink_receive_impl(void* ctx,
                                        xgl_handle_t handle,
                                        void* data) {
    (void)ctx;
    (void)handle;
    (void)data;
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
