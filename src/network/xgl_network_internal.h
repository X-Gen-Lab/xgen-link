/**
 * \file            xgl_network_internal.h
 * \brief           Internal network helpers shared across network modules
 * \author          X-Gen Lab
 */

#ifndef XGL_NETWORK_INTERNAL_H
#define XGL_NETWORK_INTERNAL_H

#include "network/xgl_network.h"
#include "wire/xgl_wire.h"

xgl_error_t xgl_network_send_generated(xgl_network_ctx_t* ctx,
                                       xgl_handle_t handle,
                                       xgl_packet_t* packet);

xgl_error_t xgl_network_check_data(void* ctx, xgl_handle_t handle,
                                   const xgl_tx_data_t* data, bool need_hello);

/** \brief           Check ACK egress using the actual reverse-scope policy. */
xgl_error_t xgl_network_check_feedback(void* ctx, const xgl_packet_t* received,
                                       size_t ack_extension_len);

/** \brief           Validate caller storage before DATA admission policy. */
xgl_error_t
xgl_network_validate_zerocopy_request(const xgl_tx_zerocopy_request_t* request);

#if XGL_FEATURE_FORWARDING
xgl_error_t xgl_network_forward(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const xgl_wire_frame_view_t* metadata);
xgl_error_t xgl_network_forward_mutable(xgl_network_ctx_t* ctx,
                                        xgl_handle_t handle,
                                        const xgl_wire_frame_view_t* metadata,
                                        uint8_t* mutable_frame);
#endif

xgl_error_t
xgl_network_receive_mutable_view(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                 const xgl_wire_frame_view_t* metadata,
                                 uint8_t* mutable_frame);

/** \brief           Count one frame rejected by network receive policy. */
static inline void xgl_network_count_rx_drop(xgl_network_ctx_t* ctx) {
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_dropped++;
    }
#else
    (void)ctx;
#endif
}

#endif /* XGL_NETWORK_INTERNAL_H */
