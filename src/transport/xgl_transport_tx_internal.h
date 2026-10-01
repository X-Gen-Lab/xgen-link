/**
 * \file            xgl_transport_tx_internal.h
 * \brief           Private planning and ownership contracts for transport TX
 */

#ifndef XGL_TRANSPORT_TX_INTERNAL_H
#define XGL_TRANSPORT_TX_INTERNAL_H

#include "transport/xgl_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Validated wire budgets shared by admission and fragmentation
 */
typedef struct {
    bool needs_fragmentation;
    size_t fragment_payload_budget;
    size_t fragment_count;
} xgl_transport_send_plan_t;

/**
 * \brief           Plan transmission without allocating or changing peer state
 * \param[in]       ctx: Transport configuration and route MTU access
 * \param[in]       tx_data: Borrowed transmission request
 * \param[out]      plan: Complete frame and fragment budgets on success
 * \return          XGL_OK or a request/budget validation error
 */
xgl_error_t transport_build_send_plan(const xgl_transport_ctx_t* ctx,
                                      const xgl_tx_data_t* tx_data,
                                      xgl_transport_send_plan_t* plan);

/**
 * \brief           Copy reliable payload and extensions into an owned record
 * \note            Unreliable traffic returns no record. Failed reservations
 *                  are released without consuming a DATA number.
 */
xgl_error_t transport_queue_reliable_tx(
    const xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    const xgl_tx_data_t* tx_data, const uint8_t* data, size_t data_len,
    uint32_t packet_number, bool fragment, const uint8_t* extensions,
    size_t extensions_len, xgl_reliable_packet_t** rel_packet);

/**
 * \brief           Submit borrowed bytes and commit a reserved reliable record
 * \note            A lower-layer error releases the reservation and clears the
 *                  caller's record pointer; success advances the DATA number.
 */
xgl_error_t transport_send_packet_view(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle,
    xgl_transport_peer_state_t* peer, const xgl_tx_data_t* tx_data,
    const uint8_t* data, size_t data_len, uint32_t packet_number, bool fragment,
    uint8_t* extensions, size_t extensions_len,
    xgl_reliable_packet_t** rel_packet);

#if XGL_FEATURE_FRAGMENTATION
/**
 * \brief           Own an admitted reliable message or submit unreliable pieces
 * \note            Reliable admission copies the complete borrowed request.
 *                  Later failures terminate its peer and report a callback.
 */
xgl_error_t transport_send_fragmented(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle,
                                      xgl_transport_peer_state_t* peer,
                                      const xgl_tx_data_t* tx_data,
                                      const xgl_transport_send_plan_t* plan);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_TX_INTERNAL_H */
