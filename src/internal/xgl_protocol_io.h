/**
 * \file            xgl_protocol_io.h
 * \brief           Typed synchronous packet and frame boundaries
 * \author          X-Gen Lab
 */

#ifndef XGL_PROTOCOL_IO_H
#define XGL_PROTOCOL_IO_H

#include "wire/xgl_frame.h"
#include "xgl/xgl_types.h"

#include "internal/xgl_packet.h"

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
 * \brief           A required validated view borrowed until receive returns
 * \note            Raw bytes must be decoded before entering this boundary.
 */
typedef struct {
    const xgl_wire_frame_view_t* view;
} xgl_frame_rx_message_t;

/**
 * \brief           Synchronous packet sender borrowing mutable packet metadata
 */
typedef struct {
    void* ctx;
    xgl_error_t (*send)(void* ctx, xgl_handle_t handle, xgl_packet_t* packet);
} xgl_packet_tx_endpoint_t;

/**
 * \brief           Synchronous packet receiver borrowing immutable metadata
 */
typedef struct {
    void* ctx;
    xgl_error_t (*receive)(void* ctx, xgl_handle_t handle,
                           const xgl_packet_t* packet);
} xgl_packet_rx_endpoint_t;

/**
 * \brief           Synchronous frame sender with explicit storage ownership
 */
typedef struct {
    void* ctx;
    xgl_error_t (*send)(void* ctx, xgl_handle_t handle,
                        const xgl_frame_tx_message_t* message);
} xgl_frame_tx_endpoint_t;

/**
 * \brief           Synchronous frame receiver borrowing validated wire storage
 */
typedef struct {
    void* ctx;
    xgl_error_t (*receive)(void* ctx, xgl_handle_t handle,
                           const xgl_frame_rx_message_t* message);
} xgl_frame_rx_endpoint_t;

/**
 * \brief           Initialize a packet sender without taking ownership
 */
static inline void xgl_packet_tx_endpoint_init(
    xgl_packet_tx_endpoint_t* iface, void* ctx,
    xgl_error_t (*send)(void*, xgl_handle_t, xgl_packet_t*)) {
    if (iface != NULL) {
        iface->ctx = ctx;
        iface->send = send;
    }
}

/**
 * \brief           Initialize a packet receiver without taking ownership
 */
static inline void xgl_packet_rx_endpoint_init(
    xgl_packet_rx_endpoint_t* iface, void* ctx,
    xgl_error_t (*receive)(void*, xgl_handle_t, const xgl_packet_t*)) {
    if (iface != NULL) {
        iface->ctx = ctx;
        iface->receive = receive;
    }
}

/**
 * \brief           Initialize a frame sender without taking ownership
 */
static inline void xgl_frame_tx_endpoint_init(
    xgl_frame_tx_endpoint_t* iface, void* ctx,
    xgl_error_t (*send)(void*, xgl_handle_t, const xgl_frame_tx_message_t*)) {
    if (iface != NULL) {
        iface->ctx = ctx;
        iface->send = send;
    }
}

/**
 * \brief           Initialize a frame receiver without taking ownership
 */
static inline void xgl_frame_rx_endpoint_init(
    xgl_frame_rx_endpoint_t* iface, void* ctx,
    xgl_error_t (*receive)(void*, xgl_handle_t,
                           const xgl_frame_rx_message_t*)) {
    if (iface != NULL) {
        iface->ctx = ctx;
        iface->receive = receive;
    }
}

/**
 * \brief           Submit a packet without extending its storage lifetime
 */
static inline xgl_error_t xgl_packet_send(const xgl_packet_tx_endpoint_t* iface,
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
xgl_packet_receive(const xgl_packet_rx_endpoint_t* iface, xgl_handle_t handle,
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
