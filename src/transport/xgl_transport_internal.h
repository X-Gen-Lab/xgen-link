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

void *transport_malloc(xgl_allocator_t *allocator, size_t size);
void transport_free(xgl_allocator_t *allocator, void *ptr);
void transport_free_rx_buffered_packet(
    xgl_transport_ctx_t *ctx, xgl_transport_rx_buffered_packet_t *buffered);

xgl_transport_peer_state_t *transport_find_peer(xgl_transport_ctx_t *ctx,
                                                uint16_t peer_id);
xgl_transport_peer_state_t *transport_find_peer_scope(xgl_transport_ctx_t *ctx,
                                                      uint16_t peer_id,
                                                      uint32_t connection_id,
                                                      uint32_t session_epoch);
xgl_transport_peer_state_t *
transport_get_or_create_peer(xgl_transport_ctx_t *ctx, uint16_t peer_id);
xgl_transport_peer_state_t *
transport_get_or_create_peer_scope(xgl_transport_ctx_t *ctx, uint16_t peer_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch);
xgl_transport_peer_state_t *transport_find_rx_peer(xgl_transport_ctx_t *ctx,
                                                   const xgl_packet_t *packet);
xgl_transport_peer_state_t *
transport_get_or_create_rx_peer(xgl_transport_ctx_t *ctx,
                                const xgl_packet_t *packet);
xgl_error_t transport_prepare_rx_peer(xgl_transport_ctx_t *ctx,
                                      const xgl_packet_t *packet,
                                      xgl_transport_peer_state_t **peer);
xgl_error_t transport_process_reliable_rx_order(
    xgl_transport_ctx_t *ctx, xgl_handle_t handle, const xgl_packet_t *packet,
    xgl_transport_peer_state_t **peer);
void transport_destroy_peers(xgl_transport_ctx_t *ctx);
void transport_commit_packet_number(xgl_transport_ctx_t *ctx,
                                    xgl_transport_peer_state_t *peer);
uint32_t transport_receive_packet_number(const xgl_packet_t *packet);
void transport_count_send_error(xgl_transport_ctx_t *ctx);
void transport_reset_peer_state(xgl_transport_ctx_t *ctx,
                                xgl_transport_peer_state_t *peer,
                                uint16_t session_id, uint32_t connection_id,
                                uint32_t session_epoch);

xgl_error_t transport_send_control(const xgl_transport_ctx_t *ctx,
                                   xgl_handle_t handle, uint16_t target_id,
                                   uint8_t control_type,
                                   uint32_t control_packet_number,
                                   uint16_t session_id, uint32_t connection_id,
                                   uint32_t session_epoch);
xgl_error_t transport_process_control_packet(xgl_transport_ctx_t *ctx,
                                             const xgl_packet_t *packet);
xgl_error_t transport_process_ack_packet(xgl_transport_ctx_t *ctx,
                                         xgl_handle_t handle,
                                         const xgl_packet_t *packet);
xgl_error_t transport_send_ack(const xgl_transport_ctx_t *ctx,
                               xgl_handle_t handle, uint32_t packet_number,
                               uint16_t source_id, uint16_t session_id,
                               uint32_t connection_id, uint32_t session_epoch);
xgl_error_t transport_send_sack(const xgl_transport_ctx_t *ctx,
                                xgl_handle_t handle,
                                const xgl_transport_peer_state_t *peer,
                                uint16_t source_id, uint32_t base_packet,
                                uint16_t session_id, uint32_t connection_id,
                                uint32_t session_epoch);

xgl_error_t transport_retransmit_reliable_packet(
    xgl_transport_ctx_t *ctx, xgl_handle_t handle,
    xgl_reliable_packet_t *rel_packet, uint32_t current_time_ms);
uint32_t transport_process_retransmissions(xgl_transport_ctx_t *ctx,
                                           xgl_handle_t handle,
                                           uint32_t current_time_ms);

xgl_error_t transport_cache_out_of_order_packet(
    xgl_transport_ctx_t *ctx, xgl_transport_peer_state_t *peer,
    const xgl_packet_t *packet, uint32_t packet_number);
void transport_clear_rx_buffered(xgl_transport_ctx_t *ctx,
                                 xgl_transport_peer_state_t *peer);
xgl_transport_rx_buffered_packet_t *
transport_take_rx_buffered(xgl_transport_peer_state_t *peer,
                           uint32_t packet_number);
xgl_error_t transport_deliver_packet(xgl_transport_ctx_t *ctx,
                                     xgl_handle_t handle,
                                     const xgl_packet_t *packet,
                                     const uint8_t *data, size_t data_len);
xgl_error_t transport_drain_rx_buffered(xgl_transport_ctx_t *ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t *peer);

xgl_error_t transport_try_process_ack_range_ext(
    xgl_transport_ctx_t *ctx, xgl_transport_peer_state_t *peer,
    uint16_t source_id, const uint8_t *data, size_t data_len, bool *handled);
xgl_error_t transport_try_process_sack_ext(xgl_transport_ctx_t *ctx,
                                           xgl_handle_t handle,
                                           xgl_transport_peer_state_t *peer,
                                           uint16_t source_id,
                                           const uint8_t *data, size_t data_len,
                                           bool *handled);

#endif /* XGL_TRANSPORT_INTERNAL_H */
