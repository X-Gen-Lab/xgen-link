/**
 * \file            xgl_network_internal.h
 * \brief           Internal network helpers shared across network modules
 */

#ifndef XGL_NETWORK_INTERNAL_H
#define XGL_NETWORK_INTERNAL_H

#include "network/xgl_network.h"
#include "wire/xgl_wire.h"

xgl_error_t xgl_network_send_with_handle(xgl_network_ctx_t* ctx,
                                         xgl_handle_t handle,
                                         xgl_packet_t* packet);

/** \brief           Validate caller storage before DATA admission policy. */
xgl_error_t
xgl_network_validate_tx_data_zerocopy(const xgl_tx_data_zerocopy_t* tx_data);

/**
 * \brief           Submit caller storage and distinguish request validation
 * \param[in,out]   ctx: Network policy and counters
 * \param[in]       handle: Instance passed to synchronous callbacks
 * \param[in]       request: Borrowed caller buffer and send metadata
 * \param[in]       connection_id: Requested connection identity
 * \param[in]       session_epoch: Requested session epoch
 * \param[out]      attempted: Optional flag, false on basic request rejection,
 *                  true once entering the common network send path
 * \return          Request validation or network submission result
 * \note            Attempted does not imply PHY success or reliable admission.
 */
xgl_error_t
xgl_network_send_zerocopy_scope(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const xgl_tx_data_zerocopy_t* request,
                                uint32_t connection_id, uint32_t session_epoch,
                                bool* attempted);

#if XGL_FEATURE_FORWARDING
xgl_error_t xgl_network_forward(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const xgl_wire_frame_view_t* metadata);
#endif

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
