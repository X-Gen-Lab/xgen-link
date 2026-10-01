/**
 * \file            xgl_send_zerocopy.c
 * \brief           Synchronous caller-buffer send API
 * \author          X-Gen Lab
 */

#include "xgl_instance_internal.h"

#include <string.h>

static void count_zerocopy_tx(xgl_handle_t handle, size_t payload_len) {
#if XGL_FEATURE_STATISTICS
    handle->stats.transport.tx_packets++;
    handle->stats.transport.tx_bytes += payload_len;
#else
    (void)handle;
    (void)payload_len;
#endif
}

/** \brief           Submit a borrowed frame through the common network path. */
xgl_error_t xgl_send_zerocopy_at(xgl_handle_t handle,
                                 const xgl_tx_data_zerocopy_t* tx_data,
                                 uint32_t now_ms) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    handle->layers.transport_ctx.current_time_ms = now_ms;
    xgl_error_t error =
        xgl_network_send_zerocopy(&handle->layers.network_ctx, handle, tx_data);
    if (error == XGL_OK) {
        count_zerocopy_tx(handle, tx_data->data_len);
    }
    return error;
}

xgl_error_t xgl_send_zerocopy_layout(xgl_handle_t handle,
                                     const xgl_tx_zerocopy_request_t* request,
                                     xgl_tx_frame_layout_t* layout) {
    if (layout == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(layout, 0, sizeof(*layout));
    if (handle == NULL || request == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    return xgl_network_zerocopy_layout(&handle->layers.network_ctx, request,
                                       layout);
}

xgl_error_t
xgl_send_zerocopy_scoped_at(xgl_handle_t handle,
                            const xgl_tx_zerocopy_request_t* request,
                            uint32_t now_ms) {
    if (handle == NULL || request == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    handle->layers.transport_ctx.current_time_ms = now_ms;
    xgl_error_t error = xgl_network_send_zerocopy_scoped(
        &handle->layers.network_ctx, handle, request);
    if (error == XGL_OK) {
        count_zerocopy_tx(handle, request->data.data_len);
    }
    return error;
}
