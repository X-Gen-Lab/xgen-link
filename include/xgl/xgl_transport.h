/**
 * \file            xgl_transport.h
 * \brief           Transport Layer Main Interface
 * \author          Nexus Team
 */

#ifndef XGL_TRANSPORT_H
#define XGL_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"
#include "xgl_types.h"
#include "xgl_rtt.h"
#include "xgl_window.h"
#include "xgl_reliable.h"
#include "xgl_ack.h"
#include "xgl_fragment.h"
#include "xgl_layer_interface.h"

/*---------------------------------------------------------------------------*/
/* Transport Layer Context                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Transport layer context structure
 * \note            Integrates all transport layer components
 */
typedef struct xgl_transport_ctx_s {
    /* Configuration */
    uint8_t local_id;               /**< Local node ID */
    uint8_t max_retry_count;        /**< Maximum retry count */
    uint32_t default_timeout_ms;    /**< Default timeout in milliseconds */
    bool enable_fragmentation;      /**< Enable fragmentation support */
    uint16_t max_frame_size;        /**< Maximum frame size */
    
    /* Transport components */
    xgl_rtt_estimator_t rtt_est;    /**< RTT estimator */
    xgl_sliding_window_t window;    /**< Sliding window */
    xgl_reliable_queue_t reliable_queue; /**< Reliable transmission queue */
    xgl_ack_handler_t ack_handler;  /**< ACK handler */
    xgl_fragment_manager_t* fragment_mgr; /**< Fragmentation manager (optional) */
    
    /* Layer interface for decoupled communication */
    xgl_layer_interface_t* lower_layer; /**< Lower layer interface (network) */
    
    /* Callbacks */
    xgl_rx_callback_t rx_callback;  /**< Receive callback */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;       /**< User data for callbacks */
    
    /* Statistics */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* tx_retries;           /**< Retransmission counter pointer */
    
    /* Memory management */
    xgl_allocator_t* allocator;     /**< Memory allocator */
    
} xgl_transport_ctx_t;

/**
 * \brief           Transport layer configuration structure
 */
typedef struct {
    uint8_t local_id;               /**< Local node ID */
    uint8_t max_retry_count;        /**< Maximum retry count */
    uint32_t default_timeout_ms;    /**< Default timeout in milliseconds */
    uint8_t window_size;            /**< Sliding window size */
    bool enable_fragmentation;      /**< Enable fragmentation support */
    uint16_t max_frame_size;        /**< Maximum frame size */
    xgl_layer_interface_t* lower_layer; /**< Lower layer interface (network) */
    xgl_rx_callback_t rx_callback;  /**< Receive callback (can be NULL) */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data;       /**< User data for callbacks (can be NULL) */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* tx_retries;           /**< Retransmission counter pointer (can be NULL) */
    xgl_allocator_t* allocator;     /**< Memory allocator (NULL = malloc/free) */
} xgl_transport_config_t;

/*---------------------------------------------------------------------------*/
/* Transport Layer API                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport layer context with configuration structure
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       config: Configuration structure
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_transport_init(xgl_transport_ctx_t* ctx,
                               const xgl_transport_config_t* config);

/**
 * \brief           Destroy transport layer context
 * \param[in,out]   ctx: Transport layer context
 */
void xgl_transport_destroy(xgl_transport_ctx_t* ctx);

/**
 * \brief           Send data through transport layer
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       tx_data: Transmission data
 * \return          XGL_OK on success, error code otherwise
 * \note            Handles fragmentation, reliable transmission, and routing
 */
xgl_error_t xgl_transport_send(xgl_transport_ctx_t* ctx,
                               xgl_handle_t handle,
                               const xgl_tx_data_t* tx_data);

/**
 * \brief           Receive and process packet from network layer
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Packet from network layer (contains metadata)
 * \return          XGL_OK on success, error code otherwise
 * \note            Processes ACKs, handles reassembly, and delivers to application
 * \note            Payload data should be in packet->data for receive path
 */
xgl_error_t xgl_transport_receive(xgl_transport_ctx_t* ctx,
                                  xgl_handle_t handle,
                                  const xgl_packet_t* packet);

/**
 * \brief           Periodic transport layer processing
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       current_time_ms: Current time in milliseconds
 * \return          XGL_OK on success, error code otherwise
 * \note            Processes timeouts, retransmissions, and fragment reassembly
 *                  Should be called periodically (e.g., every 10-100ms)
 */
xgl_error_t xgl_transport_run(xgl_transport_ctx_t* ctx,
                              xgl_handle_t handle,
                              uint32_t current_time_ms);

/**
 * \brief           Get next sequence number for target
 * \param[in]       ctx: Transport layer context
 * \return          Next sequence number
 */
uint8_t xgl_transport_get_next_seq(xgl_transport_ctx_t* ctx);

/**
 * \brief           Check if transport layer can send (window not full)
 * \param[in]       ctx: Transport layer context
 * \return          true if can send, false if window is full
 */
bool xgl_transport_can_send(const xgl_transport_ctx_t* ctx);

/**
 * \brief           Report error through error callback
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message
 */
void xgl_transport_report_error(xgl_transport_ctx_t* ctx,
                                xgl_handle_t handle,
                                xgl_error_t error,
                                const char* message);

/**
 * \brief           Get transport layer interface
 * \details         Returns the layer interface for this transport instance
 * \param[in]       ctx: Transport layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_transport_get_interface(xgl_transport_ctx_t* ctx,
                                       xgl_layer_interface_t* iface);

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_H */
