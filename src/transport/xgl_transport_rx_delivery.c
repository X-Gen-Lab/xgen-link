/**
 * \file            xgl_transport_rx_delivery.c
 * \brief           Application admission for received DATA
 */

#include "xgl_transport_rx_internal.h"

/**
 * \brief           Offer borrowed payload bytes to the application
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       source_id: Remote node ID
 * \param[in]       data_type: Application message type
 * \param[in]       data: Borrowed payload bytes
 * \param[in]       len: Payload size
 * \return          Application admission result
 */
static xgl_error_t transport_accept_payload(
#if XGL_FEATURE_ADVANCED_RUNTIME
    const xgl_rx_info_t* info,
#endif
    xgl_transport_ctx_t* ctx, xgl_handle_t handle, uint16_t source_id,
    uint8_t data_type, const uint8_t* data, size_t len) {
    xgl_error_t err = XGL_OK;
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (ctx->rx_scoped_callback != NULL) {
        err = ctx->rx_scoped_callback(handle, info, data, len,
                                      ctx->extended_user_data);
    } else {
#endif
        if (ctx->rx_accept_callback != NULL) {
            err = ctx->rx_accept_callback(handle, source_id, data_type, data,
                                          len, ctx->callback_user_data);
        } else if (ctx->rx_callback != NULL) {
            ctx->rx_callback(handle, source_id, data_type, data, len,
                             ctx->callback_user_data);
        } else {
            return XGL_ERR_UNSUPPORTED;
        }
#if XGL_FEATURE_ADVANCED_RUNTIME
    }
#endif
    if (err != XGL_OK) {
        return err;
    }
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += len;
    }
#endif
    return XGL_OK;
}

/**
 * \brief           Offer packet payload to the application
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Packet metadata
 * \param[in]       data: Borrowed payload bytes
 * \param[in]       data_len: Payload size
 * \return          XGL_OK after application admission
 */
xgl_error_t transport_deliver_packet(xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_packet_t* packet,
                                     const uint8_t* data, size_t data_len) {
    if (ctx == NULL || packet == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint16_t source_id = packet->source_id;
    uint8_t data_type = packet->data_type;

#if XGL_FEATURE_ADVANCED_RUNTIME
    const xgl_rx_info_t info = {
        source_id, packet->connection_id, packet->session_epoch, data_type,
        packet->reliable == XGL_RELIABILITY_ACK_ELICITING};
#endif
    return transport_accept_payload(
#if XGL_FEATURE_ADVANCED_RUNTIME
        &info,
#endif
        ctx, handle, source_id, data_type, data, data_len);
}
