/**
 * \file            xgl_transport_rx_internal.h
 * \brief           Private receive admission and delivery operations
 */

#ifndef XGL_TRANSPORT_RX_INTERNAL_H
#define XGL_TRANSPORT_RX_INTERNAL_H

#include "xgl_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XGL_FEATURE_OUT_OF_ORDER
void transport_free_rx_buffered_packet(
    const xgl_transport_ctx_t* ctx,
    xgl_transport_rx_buffered_packet_t* buffered);
xgl_error_t transport_cache_out_of_order_packet(
    const xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    const xgl_packet_t* packet, uint32_t packet_number);
void transport_clear_rx_buffered(const xgl_transport_ctx_t* ctx,
                                 xgl_transport_peer_state_t* peer);
xgl_transport_rx_buffered_packet_t*
transport_take_rx_buffered(xgl_transport_peer_state_t* peer,
                           uint32_t packet_number);
xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t* ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t* peer,
                                        bool* retained_backpressure);
#endif

xgl_error_t transport_deliver_packet(xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_packet_t* packet,
                                     const uint8_t* data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_RX_INTERNAL_H */
