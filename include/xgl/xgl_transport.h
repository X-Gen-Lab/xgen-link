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
#include "xgl_network.h"

/*---------------------------------------------------------------------------*/
/* Transport Layer Context                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Transport layer context structure
 * \note            Integrates all transport layer components
 */
typedef struct {
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
    
    /* Network layer context */
    xgl_network_ctx_t* network_ctx; /**< Network layer context */
    
    /* Callbacks */
    xgl_rx_callback_t rx_callback;  /**< Receive callback */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;       /**< User data for callbacks */
    
    /* Statistics */
    xgl_statistics_t* stats;        /**< Statistics structure */
    
    /* Memory management */
    xgl_allocator_t* allocator;     /**< Memory allocator */
    
} xgl_transport_ctx_t;

/*---------------------------------------------------------------------------*/
/* Transport Layer API                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport layer context
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       local_id: Local node ID
 * \param[in]       max_retry_count: Maximum retry count
 * \param[in]       default_timeout_ms: Default timeout in milliseconds
 * \param[in]       window_size: Sliding window size
 * \param[in]       enable_fragmentation: Enable fragmentation support
 * \param[in]       max_frame_size: Maximum frame size
 * \param[in]       network_ctx: Network layer context
 * \param[in]       rx_callback: Receive callback
 * \param[in]       error_callback: Error callback
 * \param[in]       callback_user_data: User data for callbacks
 * \param[in]       stats: Statistics structure
 * \param[in]       allocator: Memory allocator (NULL = malloc/free)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_transport_init(xgl_transport_ctx_t* ctx,
                               uint8_t local_id,
                               uint8_t max_retry_count,
                               uint32_t default_timeout_ms,
                               uint8_t window_size,
                               bool enable_fragmentation,
                               uint16_t max_frame_size,
                               xgl_network_ctx_t* network_ctx,
                               xgl_rx_callback_t rx_callback,
                               xgl_error_callback_t error_callback,
                               void* callback_user_data,
                               xgl_statistics_t* stats,
                               xgl_allocator_t* allocator);

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
 * \param[in]       source_id: Source node ID
 * \param[in]       target_id: Target node ID
 * \param[in]       seq_num: Sequence number
 * \param[in]       ack_num: ACK number
 * \param[in]       data_type: Data type
 * \param[in]       reliable: Reliable transmission flag
 * \param[in]       fragment: Fragment flag
 * \param[in]       data: Packet data
 * \param[in]       data_len: Data length
 * \return          XGL_OK on success, error code otherwise
 * \note            Processes ACKs, handles reassembly, and delivers to application
 */
xgl_error_t xgl_transport_receive(xgl_transport_ctx_t* ctx,
                                  xgl_handle_t handle,
                                  uint8_t source_id,
                                  uint8_t target_id,
                                  uint8_t seq_num,
                                  uint8_t ack_num,
                                  uint8_t data_type,
                                  uint8_t reliable,
                                  uint8_t fragment,
                                  const uint8_t* data,
                                  size_t data_len);

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

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_H */
