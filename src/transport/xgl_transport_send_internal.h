/**
 * \file            xgl_transport_send_internal.h
 * \brief           Private helpers shared by transport send modules
 */

#ifndef XGL_TRANSPORT_SEND_INTERNAL_H
#define XGL_TRANSPORT_SEND_INTERNAL_H

#include "xgl_transport_internal.h"
#include "xgl/internal/xgl_transport_send.h"

xgl_error_t transport_queue_reliable_tx(const xgl_transport_ctx_t* ctx,
                                        xgl_transport_peer_state_t* peer,
                                        const xgl_tx_data_t* tx_data,
                                        const uint8_t* data,
                                        size_t data_len,
                                        uint32_t packet_number,
                                        bool fragment,
                                        const uint8_t* extensions,
                                        size_t extensions_len,
                                        xgl_reliable_packet_t** rel_packet);

xgl_error_t transport_send_packet_view(xgl_transport_ctx_t* ctx,
                                       xgl_handle_t handle,
                                       xgl_transport_peer_state_t* peer,
                                       const xgl_tx_data_t* tx_data,
                                       const uint8_t* data,
                                       size_t data_len,
                                       uint32_t packet_number,
                                       bool fragment,
                                       uint8_t* extensions,
                                       size_t extensions_len,
                                       xgl_reliable_packet_t** rel_packet);

xgl_error_t transport_send_fragmented(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle,
                                      xgl_transport_peer_state_t* peer,
                                      const xgl_tx_data_t* tx_data,
                                      const xgl_transport_send_plan_t* plan);

#endif /* XGL_TRANSPORT_SEND_INTERNAL_H */
