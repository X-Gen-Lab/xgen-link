/**
 * \file            xgl_transport_interface.c
 * \brief           Transport layer interface adapter
 */

#include "xgl_transport_internal.h"

/**
 * \brief           Reject send calls through the lower-layer interface
 * \details         Applications enter transport TX through
 * xgl_transport_send().
 */
static xgl_error_t transport_send_impl(void *ctx, xgl_handle_t handle,
                                       void *data)
{
    (void) ctx;
    (void) handle;
    (void) data;
    return XGL_ERR_INVALID_PARAM;
}

/**
 * \brief           Transport layer receive implementation (called by lower
 * layers)
 * \details         This function is called by network layer to deliver packets
 */
static xgl_error_t
transport_receive_impl(void *ctx, xgl_handle_t handle,
                       // cppcheck-suppress constParameterCallback
                       void *data)
{
    xgl_transport_ctx_t *trans_ctx = (xgl_transport_ctx_t *) ctx;
    const xgl_packet_t *packet = (const xgl_packet_t *) data;

    if (trans_ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Forward to transport receive function */
    return xgl_transport_receive(trans_ctx, handle, packet);
}

/**
 * \brief           Transport layer error reporting implementation
 * \details         This function is called to report errors to application
 */
static xgl_error_t transport_report_error_impl(void *ctx, xgl_handle_t handle,
                                               void *data)
{
    xgl_transport_ctx_t *trans_ctx = (xgl_transport_ctx_t *) ctx;
    xgl_layer_error_info_t *error_info = (xgl_layer_error_info_t *) data;

    if (trans_ctx == NULL || error_info == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Forward error to callback if available */
    if (trans_ctx->error_callback != NULL) {
        trans_ctx->error_callback(handle, error_info->error,
                                  error_info->message,
                                  trans_ctx->callback_user_data);
    }

    return XGL_OK;
}

/**
 * \brief           Get transport layer interface
 * \details         Returns the layer interface for this transport instance
 * \param[in]       ctx: Transport layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_transport_get_interface(xgl_transport_ctx_t *ctx,
                                        xgl_layer_interface_t *iface)
{
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_layer_interface_init(iface, ctx, transport_send_impl,
                             transport_receive_impl,
                             transport_report_error_impl);

    return XGL_OK;
}
