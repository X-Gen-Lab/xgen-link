/**
 * \file            xgl_transport_rx_delivery.c
 * \brief           Application admission for received DATA
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-17
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "xgl_transport_rx_internal.h"

/**
 * \brief           Offer one borrowed packet to the application
 * \details         Return the application admission result before changing
 *                  receive state.
 */
xgl_error_t transport_deliver_packet(xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_packet_t* packet) {
    if (ctx->rx_callback == NULL) {
        return XGL_ERR_UNSUPPORTED;
    }
    const xgl_rx_info_t info = {
        .source_id = packet->source_id,
        .connection_id = packet->connection_id,
        .session_epoch = packet->session_epoch,
        .data_type = packet->data_type,
        .reliable = packet->reliable == XGL_RELIABILITY_ACK_ELICITING,
    };
    const xgl_packet_data_t* payload = packet->data;
    xgl_error_t err = ctx->rx_callback(handle, &info, payload->data,
                                       payload->data_len, ctx->rx_user_data);
    if (err != XGL_OK) {
        return err;
    }
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += payload->data_len;
    }
#endif
    return XGL_OK;
}
