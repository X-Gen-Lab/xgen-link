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

#if XGL_FEATURE_FRAGMENTATION
/** \brief           Decoded metadata for one admitted fragment. */
typedef struct {
    uint32_t message_id;
    uint32_t offset;
    uint32_t message_len;
} transport_rx_fragment_t;

/**
 * \brief           Validate fragment metadata before retaining or delivering
 * \param[in]       ctx: Transport context with fragmentation enabled
 * \param[in]       packet: Borrowed packet with a valid payload span
 * \param[out]      fragment: Decoded singleton fragment extension
 * \return          XGL_OK or the metadata admission error
 */
xgl_error_t transport_decode_rx_fragment(const xgl_transport_ctx_t* ctx,
                                         const xgl_packet_t* packet,
                                         transport_rx_fragment_t* fragment);

/**
 * \brief           Reject active message attribute conflicts before ACK/SACK
 * \param[in]       ctx: Transport context with fragmentation enabled
 * \param[in]       peer: Existing scope, or NULL when no state exists
 * \param[in]       packet: Borrowed packet with validated fragment metadata
 * \param[in]       fragment: Decoded fragment identity and bounds
 * \return          XGL_OK when compatible, admission error otherwise
 */
xgl_error_t transport_validate_rx_fragment_scope(
    const xgl_transport_ctx_t* ctx, const xgl_transport_peer_state_t* peer,
    const xgl_packet_t* packet, const transport_rx_fragment_t* fragment);

/**
 * \brief           Admit an already validated fragment without reparsing TLVs
 * \param[in,out]   ctx: Transport context with fragmentation enabled
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Existing scope, or NULL when one must be reserved
 * \param[in]       packet: Borrowed packet with validated fragment metadata
 * \param[in]       data: Borrowed payload bytes
 * \param[in]       data_len: Payload size
 * \param[in]       fragment: Decoded fragment identity and bounds
 * \return          XGL_OK only after transport or application admission
 */
xgl_error_t
transport_deliver_rx_fragment(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                              xgl_transport_peer_state_t* peer,
                              const xgl_packet_t* packet, const uint8_t* data,
                              size_t data_len,
                              const transport_rx_fragment_t* fragment);

void transport_clear_pending_message(xgl_transport_ctx_t* ctx,
                                     xgl_transport_peer_state_t* peer);
xgl_error_t transport_drain_pending_message(xgl_transport_ctx_t* ctx,
                                            xgl_handle_t handle,
                                            xgl_transport_peer_state_t* peer);
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
