/**
 * \file            xgl_transport.h
 * \brief           Transport Layer Main Interface
 * \author          X-Gen Lab
 */

#ifndef XGL_TRANSPORT_H
#define XGL_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "xgl/internal/xgl_reliable.h"
#include "xgl/internal/xgl_rtt.h"
#include "xgl/internal/xgl_window.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"
#if XGL_FEATURE_FRAGMENTATION
#include "xgl/internal/xgl_fragment.h"
#endif
#include "xgl/internal/xgl_packet.h"
#include "xgl/internal/xgl_protocol_io.h"
#include "xgl/internal/xgl_protocol_memory.h"
#include "xgl/internal/xgl_route.h"

/**
 * \brief           Reserved transport control data types
 * \details         Values are carried in DATA_TYPE_EXT on CONTROL packets.
 */
#define XGL_TRANSPORT_CONTROL_HELLO 0x0E
#define XGL_TRANSPORT_CONTROL_RESET 0x0F

/**
 * \brief           Application admission callback using borrowed payload bytes
 * \details         XGL_OK transfers delivery responsibility to the application.
 *                  XGL_ERR_BUSY requests a later retry without acknowledging.
 */
typedef xgl_rx_accept_fn xgl_transport_accept_fn;

/*---------------------------------------------------------------------------*/
/* Transport Layer Context                                                   */
/*---------------------------------------------------------------------------*/

#if XGL_FEATURE_OUT_OF_ORDER
typedef struct xgl_transport_rx_buffered_packet_s {
    struct xgl_transport_rx_buffered_packet_s* next; /**< Linked-list node */
    xgl_packet_t packet;           /**< Cached packet metadata */
    xgl_packet_data_t packet_data; /**< Cached packet data view */
    uint8_t* data;                 /**< Owned payload bytes */
    size_t data_len;               /**< Payload length */
    uint8_t* extensions;           /**< Owned TLV extension bytes */
    size_t extensions_len;         /**< TLV extension length */
} xgl_transport_rx_buffered_packet_t;
#endif

/* Preserve the measured profile layout; host padding is not an MCU ABI change.
 */
/* NOLINTBEGIN(clang-analyzer-optin.performance.Padding) */
typedef struct xgl_transport_peer_state_s {
    struct xgl_transport_peer_state_s* next; /**< Linked-list node */
    uint16_t peer_id;                        /**< Remote node ID */
    uint32_t connection_id; /**< Production connection ID for scoped state */
    uint32_t session_epoch; /**< Production session epoch for scoped state */
    bool hello_sent;        /**< HELLO has been sent for this session */
    bool failed; /**< Scope is terminal after failure; use a new epoch */
    xgl_sliding_window_t tx_window;      /**< Peer-specific TX window */
    xgl_reliable_queue_t reliable_queue; /**< Peer-specific wait-ACK queue */
    xgl_rtt_estimator_t rtt_est;         /**< Peer-specific RTT estimator */
    uint32_t last_active_ms;             /**< Last activity timestamp */
    uint32_t rx_next_packet_number;  /**< Next in-order packet number expected
                                        from peer */
    bool rx_has_packet_number_state; /**< Receive packet-number state
                                        initialized */
#if XGL_FEATURE_OUT_OF_ORDER
    xgl_transport_rx_buffered_packet_t*
        rx_buffered;           /**< Out-of-order RX packets */
    uint8_t rx_buffered_count; /**< Number of buffered RX packets */
#endif
#if XGL_FEATURE_FRAGMENTATION
    uint8_t* tx_message_storage; /**< Owned message allocation */
    xgl_tx_data_t tx_message;    /**< Owned message; data is NULL when idle */
    size_t tx_message_offset;    /**< Next unsent message byte */
    size_t
        tx_fragment_payload_size;  /**< Fixed payload budget for this message */
    uint32_t tx_message_id;        /**< Stable message ID across windows */
    bool tx_message_retry_pending; /**< Waiting for a local-capacity retry */
    uint32_t
        tx_message_retry_started_ms; /**< Retry delay start; zero is valid */
    uint8_t* rx_pending_message;     /**< Owned complete message awaiting
                                        application capacity */
    size_t rx_pending_message_len;
    uint8_t rx_pending_message_type;
#endif
} xgl_transport_peer_state_t;

/* NOLINTEND(clang-analyzer-optin.performance.Padding) */

/**
 * \brief           Transport layer context structure
 * \note            Integrates all transport layer components
 */
typedef struct xgl_transport_ctx_s {
    /* Configuration */
    uint16_t local_id;       /**< Local node ID */
    uint8_t max_retry_count; /**< Maximum retry count */
    uint8_t window_size;     /**< Per-peer window capacity */
    uint16_t max_peers;      /**< Maximum live peer scopes */
    size_t max_tx_packets;   /**< Global reliable packet capacity */
#if XGL_FEATURE_OUT_OF_ORDER
    size_t max_rx_buffered_packets; /**< Global out-of-order capacity */
#endif
    uint32_t current_time_ms;    /**< Caller-provided runtime clock */
    uint32_t default_timeout_ms; /**< Default timeout in milliseconds */
#if XGL_FEATURE_FRAGMENTATION
    bool enable_fragmentation; /**< Enable fragmentation support */
#endif
    uint16_t max_frame_size; /**< Maximum frame size */
    uint8_t auth_tag_len; /**< Authentication tag length reserved per frame */
    xgl_route_table_t*
        route_table; /**< Optional route table for route MTU lookup */
    uint32_t
        peer_idle_timeout_ms; /**< Unused peer reclaim timeout; 0 = disabled */
#if XGL_FEATURE_FRAGMENTATION
    size_t max_message_size; /**< Maximum accepted fragmented message bytes */
    size_t max_tx_message_bytes; /**< Aggregate pending TX message budget */
    size_t tx_message_bytes;     /**< Currently owned TX message bytes */
    uint8_t
        max_reassembly_slots; /**< Max concurrent fragment reassembly slots */
#endif

    /* Transport components */
#if XGL_FEATURE_FRAGMENTATION
    xgl_fragment_manager_t fragment_storage; /**< In-place reassembly manager */
    xgl_fragment_manager_t*
        fragment_mgr; /**< Fragmentation manager (optional) */
#endif
    xgl_transport_peer_state_t*
        peers; /**< Peer-specific reliable transport state */

    /* Layer interface for decoupled communication */
    xgl_packet_interface_t* lower_layer; /**< Lower layer interface (network) */

    /* Callbacks */
    xgl_rx_callback_t rx_callback; /**< Receive callback */
    xgl_transport_accept_fn
        rx_accept_callback; /**< Optional application admission callback */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;            /**< User data for callbacks */

    /* Statistics */
    xgl_layer_stats_t* stats; /**< Layer statistics pointer */
    uint64_t* tx_retries;     /**< Retransmission counter pointer */

    /* Memory management */
    const xgm_allocator_t*
        allocator; /**< Explicit service for initialization */
    xgl_protocol_memory_t
        memory; /**< Borrowed resource allocator descriptors */

} xgl_transport_ctx_t;

/**
 * \brief           Transport layer configuration structure
 */
typedef struct {
    uint16_t local_id;           /**< Local node ID */
    uint8_t max_retry_count;     /**< Maximum retry count */
    uint32_t default_timeout_ms; /**< Default timeout in milliseconds */
    uint8_t window_size;         /**< Sliding window size */
    bool enable_fragmentation;   /**< Enable fragmentation support */
    uint16_t max_frame_size;     /**< Maximum frame size */
    uint8_t auth_tag_len; /**< Authentication tag length reserved per frame */
    xgl_route_table_t*
        route_table; /**< Optional route table for route MTU lookup */
    xgl_packet_interface_t* lower_layer; /**< Lower layer interface (network) */
    xgl_rx_callback_t rx_callback;       /**< Receive callback (can be NULL) */
    xgl_transport_accept_fn
        rx_accept_callback; /**< Overrides rx_callback; borrowed input */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data; /**< User data for callbacks (can be NULL) */
    xgl_layer_stats_t* stats; /**< Layer statistics pointer */
    uint64_t* tx_retries; /**< Retransmission counter pointer (can be NULL) */
    const xgm_allocator_t* allocator; /**< Required allocator service */
    uint32_t
        peer_idle_timeout_ms; /**< Unused peer reclaim timeout; 0 = disabled */
    uint8_t max_reassembly_slots; /**< Max concurrent fragment reassembly slots
                                     (required when fragmentation is enabled) */
    uint16_t max_peers;           /**< Explicit live peer capacity */
    size_t max_message_size;      /**< Maximum accepted message bytes when
                                     fragmentation is enabled */
    const xgl_protocol_memory_t*
        memory;                     /**< NULL uses the explicit allocator */
    size_t max_tx_packets;          /**< Explicit global capacity */
    size_t max_rx_buffered_packets; /**< Explicit global capacity */
    size_t max_reassembly_bytes; /**< Required aggregate RX message budget when
                                    enabled */
    size_t max_tx_message_bytes; /**< Required aggregate TX message budget when
                                    enabled */
} xgl_transport_config_t;

/*---------------------------------------------------------------------------*/
/* Transport Layer API                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport layer context with configuration
 *                  structure
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
 * \brief           Retire one scope and release its bounded resource
 * reservations
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle for cancellation reporting
 * \param[in]       remote_id: Remote node ID
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Exact epoch to retire
 * \return          XGL_OK on close, XGL_ERR_NOT_FOUND if absent
 * \note            Pending admitted data produces one cancellation callback.
 *                  The caller must prevent old scope traffic before reusing
 * capacity.
 */
xgl_error_t xgl_transport_close_scope(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle, uint16_t remote_id,
                                      uint32_t connection_id,
                                      uint32_t session_epoch);

/**
 * \brief           Send data through transport layer
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       tx_data: Transmission data
 * \return          XGL_OK on success, error code otherwise
 * \note            Handles fragmentation, reliable transmission, and routing
 */
xgl_error_t xgl_transport_send(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                               const xgl_tx_data_t* tx_data);

/**
 * \brief           Receive and process packet from network layer
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Packet from network layer (contains metadata)
 * \return          XGL_OK on success, error code otherwise
 * \note            Processes ACKs, handles reassembly, and delivers to
 *                  application
 * \note            Payload data should be in packet->data for receive path
 */
xgl_error_t xgl_transport_receive(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
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
xgl_error_t xgl_transport_run(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                              uint32_t current_time_ms);

/**
 * \brief           Calculate the earliest active transport timeout
 * \param[in]       ctx: Transport layer context
 * \param[in]       now_ms: Current explicit time in milliseconds
 * \param[out]      timeout_ms: Remaining delay; zero is a valid due timeout
 * \return          true if a timer is active, false otherwise
 */
bool xgl_transport_next_timeout(const xgl_transport_ctx_t* ctx, uint32_t now_ms,
                                uint32_t* timeout_ms);

/**
 * \brief           Check send capacity for one exact peer scope
 * \param[in]       ctx: Transport layer context
 * \param[in]       peer_id: Remote node ID
 * \param[in]       connection_id: Connection scope ID
 * \param[in]       session_epoch: Session scope epoch
 * \return          true if the peer can accept a reliable transmission
 */
bool xgl_transport_can_send_to(const xgl_transport_ctx_t* ctx, uint16_t peer_id,
                               uint32_t connection_id, uint32_t session_epoch);

/**
 * \brief           Report error through error callback
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message
 */
void xgl_transport_report_error(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                xgl_error_t error, const char* message);

/**
 * \brief           Get transport layer interface
 * \details         Returns the layer interface for this transport instance
 * \param[in]       ctx: Transport layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_transport_get_interface(xgl_transport_ctx_t* ctx,
                                        xgl_packet_interface_t* iface);

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_H */
