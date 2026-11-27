/**
 * \file            xgl_send_zerocopy.c
 * \brief           Synchronous caller-buffer send API
 * \author          X-Gen Lab
 */

#include "xgl_instance_internal.h"

#include <network/xgl_network_internal.h>
#include <string.h>
#include <transport/xgl_transport_internal.h>

/** \brief           Count logical submissions independently of driver status.
 */
static xgl_error_t send_zerocopy_scope(xgl_handle_t handle,
                                       const xgl_tx_data_zerocopy_t* data,
                                       uint32_t connection_id,
                                       uint32_t session_epoch) {
    xgl_error_t error = xgl_network_validate_tx_data_zerocopy(data);
    if (error != XGL_OK) {
        return error;
    }
    error = transport_prepare_data_tx(&handle->layers.transport_ctx, handle,
                                      data->target_id, connection_id,
                                      session_epoch, data->data_len);
    if (error != XGL_OK) {
        return error;
    }

    bool* attempted_output = NULL;
#if XGL_FEATURE_STATISTICS
    bool attempted = false;
    attempted_output = &attempted;
#endif
    error = xgl_network_send_zerocopy_scope(&handle->layers.network_ctx, handle,
                                            data, connection_id, session_epoch,
                                            attempted_output);
#if XGL_FEATURE_STATISTICS
    if (attempted) {
        if (error == XGL_OK) {
            handle->stats.transport.tx_packets++;
            handle->stats.transport.tx_bytes += data->data_len;
        } else {
            handle->stats.transport.tx_errors++;
        }
    }
#endif
    return error;
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
    return send_zerocopy_scope(handle, tx_data, 0U, 0U);
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
    xgl_error_t error = xgl_network_zerocopy_layout(&handle->layers.network_ctx,
                                                    request, layout);
    if (error != XGL_OK) {
        return error;
    }
    error = transport_check_data_tx_policy(
        &handle->layers.transport_ctx, request->data.target_id,
        request->connection_id, request->session_epoch, request->data.data_len);
    if (error != XGL_OK) {
        memset(layout, 0, sizeof(*layout));
    }
    return error;
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
    return send_zerocopy_scope(handle, &request->data, request->connection_id,
                               request->session_epoch);
}
