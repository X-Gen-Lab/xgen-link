/**
 * \file            xgl_send_zerocopy.c
 * \brief           Synchronous caller-buffer send API
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "xgl_instance_internal.h"

#include <network/xgl_network_internal.h>
#include <string.h>
#include <transport/xgl_transport_internal.h>
#include <xgl/xgl_inplace.h>

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
        &handle->layers.transport_ctx, request->target_id,
        request->connection_id, request->session_epoch);
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
    xgl_transport_ctx_t* transport = &handle->layers.transport_ctx;
    transport->current_time_ms = now_ms;

    /* Reject malformed storage before policy checks can expire the scope. */
    xgl_error_t error = xgl_network_validate_zerocopy_request(request);
    if (error != XGL_OK) {
        return error;
    }
    error = transport_prepare_data_tx(transport, handle, request->target_id,
                                      request->connection_id,
                                      request->session_epoch);
    if (error != XGL_OK) {
        return error;
    }

    error = xgl_network_send_zerocopy_scoped(&handle->layers.network_ctx,
                                             handle, request);
#if XGL_FEATURE_STATISTICS
    if (error == XGL_OK) {
        handle->stats.transport.tx_packets++;
        handle->stats.transport.tx_bytes += request->data_len;
    } else {
        handle->stats.transport.tx_errors++;
    }
#endif
    return error;
}
