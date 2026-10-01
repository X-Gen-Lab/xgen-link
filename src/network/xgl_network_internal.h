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

#if XGL_FEATURE_FORWARDING
xgl_error_t xgl_network_forward(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const uint8_t* frame_buf, size_t frame_len,
                                const xgl_wire_frame_view_t* metadata);
#endif

#endif /* XGL_NETWORK_INTERNAL_H */
