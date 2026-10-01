/**
 * \file            xgl_transport_internal.h
 * \brief           Internal transport helpers shared across transport modules
 */

#ifndef XGL_TRANSPORT_INTERNAL_H
#define XGL_TRANSPORT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xgl/internal/xgl_transport.h"

/**
 * \brief           Read the selected transport clock; explicit zero is valid
 * \param[in]       ctx: Transport layer context
 * \return          Current time in milliseconds
 */
static inline uint32_t transport_now(const xgl_transport_ctx_t* ctx) {
    return ctx->current_time_ms;
}

/**
 * \brief           Bound local PHY or allocator retry delays without busy
 *                  looping
 * \param[in]       ctx: Transport context
 * \return          Configured default ACK timeout clamped to 1 through 100 ms
 */
static inline uint32_t
transport_retry_delay_ms(const xgl_transport_ctx_t* ctx) {
    uint32_t delay = ctx->default_timeout_ms;
    if (delay == 0U) {
        return 1U;
    }
    return delay > 100U ? 100U : delay;
}

/**
 * \brief           Query whether a peer can accept another application send
 * \param[in]       peer: Peer state
 * \return          true if no failed or pending message blocks its window
 */
static inline bool
transport_peer_can_send(const xgl_transport_peer_state_t* peer) {
#if XGL_FEATURE_FRAGMENTATION
    if (peer->tx_message.data != NULL) {
        return false;
    }
#endif
    return !peer->failed && peer->tx_window.next_packet_number != UINT32_MAX &&
           xgl_window_can_send_packet_number(&peer->tx_window);
}

/**
 * \brief           Count live reliable packet reservations across peer scopes
 * \param[in]       ctx: Transport context
 * \return          Number of globally owned reliable packets
 */
static inline size_t transport_tx_packet_count(const xgl_transport_ctx_t* ctx) {
    size_t count = 0U;
    for (const xgl_transport_peer_state_t* peer = ctx->peers; peer != NULL;
         peer = peer->next) {
        count += xgl_reliable_get_count(&peer->reliable_queue);
    }
    return count;
}

#if XGL_FEATURE_OUT_OF_ORDER
void transport_free_rx_buffered_packet(
    const xgl_transport_ctx_t* ctx,
    xgl_transport_rx_buffered_packet_t* buffered);
#endif

xgl_transport_peer_state_t* transport_find_peer_scope(xgl_transport_ctx_t* ctx,
                                                      uint16_t peer_id,
                                                      uint32_t connection_id,
                                                      uint32_t session_epoch);
xgl_transport_peer_state_t*
transport_get_or_create_peer_scope(xgl_transport_ctx_t* ctx, uint16_t peer_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch);
xgl_transport_peer_state_t* transport_find_rx_peer(xgl_transport_ctx_t* ctx,
                                                   const xgl_packet_t* packet);
xgl_transport_peer_state_t*
transport_get_or_create_rx_peer(xgl_transport_ctx_t* ctx,
                                const xgl_packet_t* packet);
bool transport_peer_can_reclaim(const xgl_transport_ctx_t* ctx,
                                const xgl_transport_peer_state_t* peer);
void transport_destroy_peers(xgl_transport_ctx_t* ctx);
uint32_t transport_reclaim_idle_peers(xgl_transport_ctx_t* ctx,
                                      uint32_t current_time_ms);
void transport_count_send_error(xgl_transport_ctx_t* ctx);
void transport_fail_peer(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                         xgl_transport_peer_state_t* peer, xgl_error_t error);
#if XGL_FEATURE_FRAGMENTATION
void transport_clear_tx_message(xgl_transport_ctx_t* ctx,
                                xgl_transport_peer_state_t* peer);
xgl_error_t transport_pump_tx_message(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle,
                                      xgl_transport_peer_state_t* peer);
void transport_clear_pending_message(xgl_transport_ctx_t* ctx,
                                     xgl_transport_peer_state_t* peer);
xgl_error_t transport_drain_pending_message(xgl_transport_ctx_t* ctx,
                                            xgl_handle_t handle,
                                            xgl_transport_peer_state_t* peer);
#endif

xgl_error_t transport_send_control(const xgl_transport_ctx_t* ctx,
                                   xgl_handle_t handle, uint16_t target_id,
                                   uint8_t control_type, uint32_t connection_id,
                                   uint32_t session_epoch);
xgl_error_t transport_process_control_packet(xgl_transport_ctx_t* ctx,
                                             xgl_handle_t handle,
                                             const xgl_packet_t* packet);
xgl_error_t transport_process_ack_packet(xgl_transport_ctx_t* ctx,
                                         xgl_handle_t handle,
                                         const xgl_packet_t* packet);
xgl_error_t transport_send_ack(const xgl_transport_ctx_t* ctx,
                               xgl_handle_t handle, uint32_t packet_number,
                               uint16_t source_id, uint32_t connection_id,
                               uint32_t session_epoch);
xgl_error_t transport_send_sack(const xgl_transport_ctx_t* ctx,
                                xgl_handle_t handle,
                                const xgl_transport_peer_state_t* peer,
                                uint16_t source_id, uint32_t base_packet,
                                uint32_t connection_id, uint32_t session_epoch);

xgl_error_t transport_retransmit_reliable_packet(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle,
    xgl_transport_peer_state_t* peer, xgl_reliable_packet_t* rel_packet,
    uint32_t current_time_ms);

#if XGL_FEATURE_OUT_OF_ORDER
xgl_error_t transport_cache_out_of_order_packet(
    const xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    const xgl_packet_t* packet, uint32_t packet_number);
void transport_clear_rx_buffered(const xgl_transport_ctx_t* ctx,
                                 xgl_transport_peer_state_t* peer);
xgl_transport_rx_buffered_packet_t*
transport_take_rx_buffered(xgl_transport_peer_state_t* peer,
                           uint32_t packet_number);
#endif
xgl_error_t transport_deliver_packet(xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_packet_t* packet,
                                     const uint8_t* data, size_t data_len);
#if XGL_FEATURE_OUT_OF_ORDER
xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t* ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t* peer);

#endif

#endif /* XGL_TRANSPORT_INTERNAL_H */
