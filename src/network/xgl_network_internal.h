/**
 * \file            xgl_network_internal.h
 * \brief           Internal network helpers shared across network modules
 */

#ifndef XGL_NETWORK_INTERNAL_H
#define XGL_NETWORK_INTERNAL_H

#include "xgl/internal/xgl_network.h"

xgl_error_t xgl_network_send_with_handle(xgl_network_ctx_t* ctx,
                                         xgl_handle_t handle,
                                         xgl_packet_t* packet,
                                         bool assign_packet_number);

#endif /* XGL_NETWORK_INTERNAL_H */
