/**
 * \file            xgl_transport_internal.h
 * \brief           Internal transport helpers shared across transport modules
 */

#ifndef XGL_TRANSPORT_INTERNAL_H
#define XGL_TRANSPORT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transport/xgl_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief           Maximum lifetime of an unused received HELLO reservation.
 */
#define XGL_TRANSPORT_HELLO_LEASE_MS 5000U

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
 * \return          true if the active peer's window allows another packet
 */
static inline bool
transport_peer_can_send(const xgl_transport_peer_state_t* peer) {
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

#if XGL_FEATURE_ADVANCED_RUNTIME
void transport_note_tx_admission(xgl_transport_ctx_t* ctx,
                                 xgl_transport_peer_state_t* peer);
void transport_notify_tx_drained(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                 xgl_transport_peer_state_t* peer);
bool transport_expire_tx(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                         xgl_transport_peer_state_t* peer);
#endif

/** \brief           Test maintenance capacity without consuming an attempt. */
static inline bool
transport_tx_budget_available(const xgl_transport_ctx_t* ctx,
                              xgl_transport_peer_state_t* peer) {
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (ctx->work != NULL && ctx->work->remaining == 0U) {
        if (peer != NULL) {
            peer->work_blocked = true;
        }
        return false;
    }
#else
    (void)ctx;
    (void)peer;
#endif
    return true;
}

/** \brief           Charge a lower-layer submission only during bounded work.
 */
static inline xgl_error_t
transport_submit_packet(const xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                        xgl_packet_t* packet) {
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (!transport_tx_budget_available(ctx, NULL)) {
        return XGL_ERR_BUSY;
    }
    if (ctx->work != NULL) {
        ctx->work->remaining--;
    }
#endif
    return xgl_packet_send(ctx->lower_layer, handle, packet);
}

xgl_transport_peer_state_t*
transport_find_peer_scope(const xgl_transport_ctx_t* ctx, uint16_t peer_id,
                          uint32_t connection_id, uint32_t session_epoch);
xgl_transport_peer_state_t*
transport_get_or_create_peer_scope(xgl_transport_ctx_t* ctx, uint16_t peer_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch);
xgl_transport_peer_state_t* transport_find_rx_peer(xgl_transport_ctx_t* ctx,
                                                   const xgl_packet_t* packet);
xgl_transport_peer_state_t*
transport_get_or_create_rx_peer(xgl_transport_ctx_t* ctx,
                                const xgl_packet_t* packet);
bool transport_peer_can_reclaim(const xgl_transport_peer_state_t* peer);
/**
 * \brief           Query the earliest unused-scope reclaim deadline
 * \param[in]       ctx: Transport configuration
 * \param[in]       peer: Scope with no reliable history or owned bytes
 * \param[in]       now_ms: Caller-provided current time
 * \param[out]      timeout_ms: Remaining delay; zero means due
 * \return          true when a HELLO lease or configured idle limit applies
 */
bool transport_peer_reclaim_timeout(const xgl_transport_ctx_t* ctx,
                                    const xgl_transport_peer_state_t* peer,
                                    uint32_t now_ms, uint32_t* timeout_ms);
void transport_destroy_peers(xgl_transport_ctx_t* ctx);
uint32_t transport_reclaim_idle_peers(xgl_transport_ctx_t* ctx,
                                      uint32_t current_time_ms);
void transport_count_send_error(xgl_transport_ctx_t* ctx);

/**
 * \brief           Check exact terminal scope without mutation
 * \param[in]       ctx: Transport state
 * \param[in]       target_id: Remote DATA destination
 * \param[in]       connection_id: Exact connection identity
 * \param[in]       session_epoch: Exact session epoch
 * \return          XGL_OK or a terminal-scope error
 */
xgl_error_t transport_check_data_tx_policy(const xgl_transport_ctx_t* ctx,
                                           uint16_t target_id,
                                           uint32_t connection_id,
                                           uint32_t session_epoch);

/** \brief           Enforce scope lifetime and terminal state before TX. */
xgl_error_t transport_prepare_data_tx(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle, uint16_t target_id,
                                      uint32_t connection_id,
                                      uint32_t session_epoch);

void transport_fail_peer(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                         xgl_transport_peer_state_t* peer, xgl_error_t error);

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
/** \brief           Check deterministic local feedback egress before RX
 * admission. */
xgl_error_t transport_check_feedback(const xgl_transport_ctx_t* ctx,
                                     const xgl_packet_t* received);
xgl_error_t transport_send_sack(const xgl_transport_ctx_t* ctx,
                                xgl_handle_t handle,
                                const xgl_transport_peer_state_t* peer,
                                uint16_t source_id, uint32_t base_packet,
                                uint32_t connection_id, uint32_t session_epoch);

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_INTERNAL_H */
