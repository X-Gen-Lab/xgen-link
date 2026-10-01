/**
 * \file            xgl_transport_rx_internal.h
 * \brief           Private receive admission and delivery operations
 */

#ifndef XGL_TRANSPORT_RX_INTERNAL_H
#define XGL_TRANSPORT_RX_INTERNAL_H

#include "xgl_transport_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XGL_FEATURE_FRAGMENTATION
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
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_RX_INTERNAL_H */
