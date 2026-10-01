/**
 * \file            xgl_send_zerocopy.c
 * \brief           Synchronous caller-buffer send API
 * \author          X-Gen Lab
 */

#include "xgl_instance_internal.h"

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
        handle->stats.transport.tx_packets++;
        handle->stats.transport.tx_bytes += tx_data->data_len;
    }
    return error;
}
