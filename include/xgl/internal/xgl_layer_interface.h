/**
 * \file            xgl_layer_interface.h
 * \brief           Layer interface definitions for decoupling
 * \author          X-Gen Lab
 */

#ifndef XGL_LAYER_INTERFACE_H
#define XGL_LAYER_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"
#include "xgl/internal/xgl_frame.h"

/*---------------------------------------------------------------------------*/
/* Forward Declarations                                                      */
/*---------------------------------------------------------------------------*/

typedef struct xgl_layer_interface_s xgl_layer_interface_t;
typedef struct xgl_packet xgl_packet_t;

/*---------------------------------------------------------------------------*/
/* Layer Interface Callbacks                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Layer operation function type
 * \details         Unified function signature for all layer operations
 * \param[in]       ctx: Layer context (opaque)
 * \param[in]       handle: Protocol instance handle
 * \param[in]       data: Operation-specific data (opaque)
 * \return          XGL_OK on success, error code otherwise
 *
 * \note            The 'data' parameter interpretation depends on operation:
 *                  - For send/receive: points to xgl_packet_t
 *                  - For error: points to xgl_layer_error_info_t
 */
typedef xgl_error_t (*xgl_layer_operation_fn)(void* ctx,
                                              xgl_handle_t handle,
                                              void* data);

/**
 * \brief           Error information structure
 * \details         Used when reporting errors through layer interface
 */
typedef struct {
    xgl_error_t error;              /**< Error code */
    const char* message;            /**< Error message */
} xgl_layer_error_info_t;

/**
 * \brief           Frame transmit message for network-to-datalink calls
 */
typedef struct {
    xgl_frame_t* frame;             /**< Frame to transmit */
    xgl_phy_ops_t* phy;             /**< Egress PHY */
} xgl_frame_tx_message_t;

/**
 * \brief           Frame receive message for datalink-to-network calls
 */
typedef struct {
    const uint8_t* frame_buf;       /**< Complete serialized frame */
    size_t frame_len;               /**< Serialized frame length */
} xgl_frame_rx_message_t;

/*---------------------------------------------------------------------------*/
/* Layer Interface Structure                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Layer interface for decoupled communication
 * \details         Each layer exposes this interface to communicate with
 *                  adjacent layers without direct context references
 */
struct xgl_layer_interface_s {
    void* ctx;                          /**< Layer context (opaque) */
    xgl_layer_operation_fn send;        /**< Send to lower layer */
    xgl_layer_operation_fn receive;     /**< Receive from lower layer */
    xgl_layer_operation_fn report_error; /**< Report error to upper layer */
};

/*---------------------------------------------------------------------------*/
/* Layer Context Manager                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Unified layer contexts
 * \details         Manages all protocol layer contexts in one place
 * \note            This is an opaque structure. The actual definition is in
 *                  xgl_instance_internal.h to avoid circular dependencies.
 */
typedef struct xgl_layer_contexts_s xgl_layer_contexts_t;

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize layer interface
 * \param[out]      iface: Layer interface to initialize
 * \param[in]       ctx: Layer context
 * \param[in]       send_fn: Send function
 * \param[in]       receive_fn: Receive function
 * \param[in]       error_fn: Error reporting function
 */
static inline void xgl_layer_interface_init(xgl_layer_interface_t* iface,
                                           void* ctx,
                                           xgl_layer_operation_fn send_fn,
                                           xgl_layer_operation_fn receive_fn,
                                           xgl_layer_operation_fn error_fn) {
    if (iface != NULL) {
        iface->ctx = ctx;
        iface->send = send_fn;
        iface->receive = receive_fn;
        iface->report_error = error_fn;
    }
}

/**
 * \brief           Send packet through layer interface
 * \param[in]       iface: Layer interface
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Packet to send
 * \return          XGL_OK on success, error code otherwise
 */
static inline xgl_error_t xgl_layer_send(const xgl_layer_interface_t* iface,
                                        xgl_handle_t handle,
                                        xgl_packet_t* packet) {
    if (iface == NULL || iface->send == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return iface->send(iface->ctx, handle, packet);
}

/**
 * \brief           Receive packet through layer interface
 * \param[in]       iface: Layer interface
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Received packet
 * \return          XGL_OK on success, error code otherwise
 */
static inline xgl_error_t xgl_layer_receive(const xgl_layer_interface_t* iface,
                                           xgl_handle_t handle,
                                           xgl_packet_t* packet) {
    if (iface == NULL || iface->receive == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return iface->receive(iface->ctx, handle, packet);
}

/**
 * \brief           Report error through layer interface
 * \param[in]       iface: Layer interface
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message
 * \return          XGL_OK on success, error code otherwise
 */
static inline xgl_error_t xgl_layer_report_error(const xgl_layer_interface_t* iface,
                                                 xgl_handle_t handle,
                                                 xgl_error_t error,
                                                 const char* message) {
    if (iface == NULL || iface->report_error == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_layer_error_info_t error_info = {
        .error = error,
        .message = message
    };

    return iface->report_error(iface->ctx, handle, &error_info);
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_LAYER_INTERFACE_H */
