/**
 * \file            xgl_protocol_io.h
 * \brief           Typed synchronous packet and frame boundaries
 * \author          X-Gen Lab
 */

#ifndef XGL_PROTOCOL_IO_H
#define XGL_PROTOCOL_IO_H

#include "xgl/internal/xgl_frame.h"
#include "xgl/xgl_types.h"

#include "xgl/internal/xgl_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           A frame borrowed until the synchronous send returns
 * \details         A logical frame uses bounded scratch storage unless buffer
 *                  supplies caller-owned storage with an exact payload offset.
 *                  A NULL frame selects serialized bytes for forwarding.
 *                  All storage remains valid until the submission returns.
 */
typedef struct {
    const xgl_frame_t* frame;
    xgl_phy_ops_t* phy;
    uint8_t* buffer;       /**< Caller frame storage for in-place encoding */
    size_t buffer_size;    /**< Available caller storage bytes */
    size_t payload_offset; /**< Required unchanged payload position */
    const uint8_t*
        serialized;        /**< Borrowed complete frame when frame is NULL */
    size_t serialized_len; /**< Complete frame bytes submitted unchanged */
} xgl_frame_tx_message_t;

/**
 * \brief           A validated frame borrowed until receive returns
 */
typedef struct {
    const uint8_t* frame_buf;
    size_t frame_len;
    const xgl_wire_frame_view_t* view;
} xgl_frame_rx_message_t;

/**
 * \brief           Transport/network packet operations with explicit types
 */
typedef struct {
    void* ctx;
    xgl_error_t (*send)(void* ctx, xgl_handle_t handle, xgl_packet_t* packet);
    xgl_error_t (*receive)(void* ctx, xgl_handle_t handle,
                           const xgl_packet_t* packet);
} xgl_packet_interface_t;

/**
 * \brief           Network/datalink frame operations with explicit types
 */
typedef struct {
    void* ctx;
    xgl_error_t (*send)(void* ctx, xgl_handle_t handle,
                        const xgl_frame_tx_message_t* message);
    xgl_error_t (*receive)(void* ctx, xgl_handle_t handle,
                           const xgl_frame_rx_message_t* message);
} xgl_frame_interface_t;

/**
 * \brief           Initialize a typed packet endpoint
 */
static inline void xgl_packet_interface_init(
    xgl_packet_interface_t* iface, void* ctx,
    xgl_error_t (*send)(void*, xgl_handle_t, xgl_packet_t*),
    xgl_error_t (*receive)(void*, xgl_handle_t, const xgl_packet_t*)) {
    if (iface != NULL) {
        iface->ctx = ctx;
        iface->send = send;
        iface->receive = receive;
    }
}

/**
 * \brief           Initialize a typed frame endpoint
 */
static inline void xgl_frame_interface_init(
    xgl_frame_interface_t* iface, void* ctx,
    xgl_error_t (*send)(void*, xgl_handle_t, const xgl_frame_tx_message_t*),
    xgl_error_t (*receive)(void*, xgl_handle_t,
                           const xgl_frame_rx_message_t*)) {
    if (iface != NULL) {
        iface->ctx = ctx;
        iface->send = send;
        iface->receive = receive;
    }
}

/**
 * \brief           Submit a packet without extending its storage lifetime
 */
static inline xgl_error_t xgl_packet_send(const xgl_packet_interface_t* iface,
                                          xgl_handle_t handle,
                                          xgl_packet_t* packet) {
    if (iface == NULL || iface->send == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return iface->send(iface->ctx, handle, packet);
}

/**
 * \brief           Deliver a borrowed packet and return receive acceptance
 */
static inline xgl_error_t
xgl_packet_receive(const xgl_packet_interface_t* iface, xgl_handle_t handle,
                   const xgl_packet_t* packet) {
    if (iface == NULL || iface->receive == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return iface->receive(iface->ctx, handle, packet);
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_PROTOCOL_IO_H */
