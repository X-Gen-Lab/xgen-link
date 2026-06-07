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
#include "xgl_fragment.h"
#include "xgl_layer_interface.h"
#include "xgl_packet_pool.h"
#include "xgl_route.h"

/**
 * \brief           Reserved transport control data types
 * \details         Frame data_type is 4-bit wide. Values 0x0E and 0x0F are
 *                  reserved for transport session lifecycle control.
 */
#define XGL_TRANSPORT_CONTROL_HELLO 0x0E
#define XGL_TRANSPORT_CONTROL_RESET 0x0F
#define XGL_TRANSPORT_CONTROL_NACK  0x0D
#define XGL_TRANSPORT_CONTROL_SACK  0x0C

/*---------------------------------------------------------------------------*/
/* Transport Layer Context                                                   */
/*---------------------------------------------------------------------------*/

typedef struct xgl_transport_rx_buffered_packet_s {
    struct xgl_transport_rx_buffered_packet_s* next; /**< Linked-list node */
    xgl_packet_t packet;                             /**< Cached packet metadata */
    xgl_packet_data_t packet_data;                   /**< Cached packet data view */
    uint8_t* data;                                   /**< Owned payload bytes */
    size_t data_len;                                 /**< Payload length */
    uint8_t* extensions;                             /**< Owned TLV extension bytes */
    size_t extensions_len;                           /**< TLV extension length */
} xgl_transport_rx_buffered_packet_t;

typedef struct xgl_transport_peer_state_s {
    struct xgl_transport_peer_state_s* next; /**< Linked-list node */
    uint16_t peer_id;                        /**< Remote node ID */
    bool has_connection_scope;               /**< Peer state is scoped by connection/session */
    uint32_t connection_id;                  /**< Production connection ID for scoped state */
    uint32_t session_epoch;                  /**< Production session epoch for scoped state */
    uint16_t session_id;                     /**< Peer transport session/epoch */
    bool hello_sent;                         /**< HELLO has been sent for this session */
    bool session_established;                /**< Peer session is known locally */
    xgl_sliding_window_t tx_window;          /**< Peer-specific TX window */
    xgl_reliable_queue_t reliable_queue;     /**< Peer-specific wait-ACK queue */
    xgl_rtt_estimator_t rtt_est;             /**< Peer-specific RTT estimator */
    uint32_t last_active_ms;                 /**< Last activity timestamp */
    uint32_t rx_next_packet_number;          /**< Next in-order packet number expected from peer */
    bool rx_has_packet_number_state;         /**< Receive packet-number state initialized */
    xgl_transport_rx_buffered_packet_t* rx_buffered; /**< Out-of-order RX packets */
    uint8_t rx_buffered_count;               /**< Number of buffered RX packets */
} xgl_transport_peer_state_t;

/**
 * \brief           Transport layer context structure
 * \note            Integrates all transport layer components
 */
typedef struct xgl_transport_ctx_s {
    /* Configuration */
    uint16_t local_id;              /**< Local node ID */
    uint8_t max_retry_count;        /**< Maximum retry count */
    uint32_t default_timeout_ms;    /**< Default timeout in milliseconds */
    bool enable_fragmentation;      /**< Enable fragmentation support */
    uint16_t max_frame_size;        /**< Maximum frame size */
    xgl_route_table_t* route_table; /**< Optional route table for route MTU lookup */
    uint16_t next_session_id;       /**< Next local session/epoch ID */
    
    /* Transport components */
    xgl_rtt_estimator_t rtt_est;    /**< RTT estimator */
    xgl_sliding_window_t window;    /**< Sliding window */
    xgl_reliable_queue_t reliable_queue; /**< Reliable transmission queue */
    xgl_fragment_manager_t* fragment_mgr; /**< Fragmentation manager (optional) */
    xgl_transport_peer_state_t* peers; /**< Peer-specific reliable transport state */
    
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
    uint16_t local_id;              /**< Local node ID */
    uint8_t max_retry_count;        /**< Maximum retry count */
    uint32_t default_timeout_ms;    /**< Default timeout in milliseconds */
    uint8_t window_size;            /**< Sliding window size */
    bool enable_fragmentation;      /**< Enable fragmentation support */
    uint16_t max_frame_size;        /**< Maximum frame size */
    xgl_route_table_t* route_table; /**< Optional route table for route MTU lookup */
    xgl_layer_interface_t* lower_layer; /**< Lower layer interface (network) */
    xgl_rx_callback_t rx_callback;  /**< Receive callback (can be NULL) */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data;       /**< User data for callbacks (can be NULL) */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* tx_retries;           /**< Retransmission counter pointer (can be NULL) */
    xgl_allocator_t* allocator;     /**< Memory allocator; NULL fallback is build-policy controlled */
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
 * \brief           Get next packet number for target
 * \param[in]       ctx: Transport layer context
 * \return          Next packet number
 */
uint32_t xgl_transport_get_next_packet_number(xgl_transport_ctx_t* ctx);

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
