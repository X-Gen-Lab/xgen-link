/**
 * \file            xgl_transport.h
 * \brief           Transport Layer Main Interface
 * \author          X-Gen Lab
 */

#ifndef XGL_TRANSPORT_H
#define XGL_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "internal/xgl_packet.h"
#include "internal/xgl_protocol_io.h"
#include "internal/xgl_protocol_memory.h"
#include "network/xgl_route.h"
#include "transport/xgl_reliable.h"
#include "transport/xgl_rtt.h"
#include "transport/xgl_window.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    uint8_t* storage; /**< Owned contiguous payload and TLV bytes */
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
    bool hello_reserved; /**< Received HELLO holds an unused scope lease */
#if XGL_FEATURE_ADVANCED_RUNTIME
    bool tx_busy;           /**< Accepted reliable TX remains unacknowledged */
    uint32_t tx_started_ms; /**< Absolute start of the current TX busy period */
#endif
    xgl_sliding_window_t tx_window;      /**< Peer-specific TX window */
    xgl_reliable_queue_t reliable_queue; /**< Peer-specific wait-ACK queue */
#if XGL_FEATURE_ADAPTIVE_RTO
    xgl_rtt_estimator_t rtt_est; /**< Peer-specific RTT estimator */
#endif
    uint32_t last_active_ms; /**< Last activity timestamp */
    uint32_t
        hello_reserved_ms; /**< First HELLO time; duplicates do not renew */
    uint32_t rx_next_packet_number;  /**< Next in-order packet number expected
                                        from peer */
    bool rx_has_packet_number_state; /**< Receive packet-number state
                                        initialized */
#if XGL_FEATURE_ADVANCED_RUNTIME
    bool work_blocked; /**< This maintenance pass stopped at its TX budget */
#endif
#if XGL_FEATURE_OUT_OF_ORDER
    xgl_transport_rx_buffered_packet_t*
        rx_buffered;           /**< Out-of-order RX packets */
    uint8_t rx_buffered_count; /**< Number of buffered RX packets */
#endif
} xgl_transport_peer_state_t;

/* NOLINTEND(clang-analyzer-optin.performance.Padding) */

/**
 * \brief           Transport layer context structure
 * \note            Integrates all transport layer components
 */
#if XGL_FEATURE_ADVANCED_RUNTIME
/** \brief           Stack-owned limits borrowed during one maintenance call. */
typedef struct {
    size_t remaining;
    xgl_transport_peer_state_t* first;
} xgl_transport_work_t;
#endif

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
    uint16_t max_frame_size;     /**< Maximum frame size */
    uint32_t
        peer_idle_timeout_ms; /**< Unused peer reclaim timeout; 0 = disabled */

    /* Transport components */
    xgl_transport_peer_state_t*
        peers; /**< Peer-specific reliable transport state */

#if XGL_FEATURE_ADVANCED_RUNTIME
    uint32_t tx_lifetime_ms;
    xgl_rx_scoped_fn rx_scoped_callback;
    xgl_scope_event_fn scope_event_callback;
    void* extended_user_data;
    xgl_transport_peer_state_t* maintenance_cursor;
    xgl_transport_work_t* work; /**< Borrowed only within bounded maintenance */
#endif

    /* Layer interface for decoupled communication */
    xgl_packet_tx_endpoint_t*
        lower_layer; /**< Lower layer interface (network) */

    /* Callbacks */
    xgl_rx_callback_t rx_callback; /**< Receive callback */
    xgl_transport_accept_fn
        rx_accept_callback; /**< Optional application admission callback */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;            /**< User data for callbacks */

    /* Statistics */
#if XGL_FEATURE_STATISTICS
    xgl_layer_stats_t* stats; /**< Layer statistics pointer */
    uint64_t* tx_retries;     /**< Retransmission counter pointer */
    uint64_t rtt_total_ms;    /**< Sum of admitted first-transmission samples */
    uint32_t
        rtt_sample_count; /**< Number of samples, saturated at UINT32_MAX */
    uint32_t rtt_min_ms;  /**< Minimum sample; UINT32_MAX when empty */
    uint32_t rtt_max_ms;  /**< Maximum admitted sample */
#endif

    /* Memory management */
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
    uint16_t max_frame_size;     /**< Maximum frame size */
    xgl_packet_tx_endpoint_t*
        lower_layer;               /**< Lower layer interface (network) */
    xgl_rx_callback_t rx_callback; /**< Receive callback (can be NULL) */
    xgl_transport_accept_fn
        rx_accept_callback; /**< Overrides rx_callback; borrowed input */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data; /**< User data for callbacks (can be NULL) */
    xgl_layer_stats_t* stats; /**< Layer statistics pointer */
    uint64_t* tx_retries; /**< Retransmission counter pointer (can be NULL) */
    const xgl_allocator_t* allocator; /**< Required allocator service */
    uint32_t
        peer_idle_timeout_ms; /**< Unused peer reclaim timeout; 0 = disabled */
    uint16_t max_peers;       /**< Explicit live peer capacity */
    const xgl_protocol_memory_t*
        memory;                     /**< NULL uses the explicit allocator */
    size_t max_tx_packets;          /**< Explicit global capacity */
    size_t max_rx_buffered_packets; /**< Explicit global capacity */
#if XGL_FEATURE_ADVANCED_RUNTIME
    uint32_t tx_lifetime_ms; /**< Zero leaves the TX busy period unlimited */
    xgl_rx_scoped_fn rx_scoped_callback;
    xgl_scope_event_fn scope_event_callback;
    void* extended_user_data;
#endif
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

#if XGL_FEATURE_ADVANCED_RUNTIME
/**
 * \brief           Bound maintenance submission attempts and rotate peer order
 * \param[in,out]   ctx: Initialized, serialized transport owner
 * \param[in]       handle: Instance passed to synchronous callbacks
 * \param[in]       now_ms: Current explicit transport time
 * \param[in]       tx_budget: Lower submission attempts; SIZE_MAX is unlimited
 * \param[out]      pending: Surviving work was blocked by this call's budget
 * \return          First maintenance error; pending is independent of errors
 * \note            Zero budget still expires owned work. Scans and application
 *                  callbacks do not count as lower submission attempts.
 */
xgl_error_t xgl_transport_run_bounded(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle, uint32_t now_ms,
                                      size_t tx_budget, bool* pending);
/**
 * \brief           Count current owners without allocation or state mutation
 * \param[in]       ctx: Initialized, serialized transport owner
 * \param[out]      resources: Current objects and payload bytes, not reserved
 * RAM
 * \return          XGL_OK, or XGL_ERR_NULL_POINTER for missing arguments
 */
xgl_error_t xgl_transport_get_resources(const xgl_transport_ctx_t* ctx,
                                        xgl_resource_snapshot_t* resources);
#endif

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
 * \note            Validates one DATA frame and tracks reliable transmission.
 */
xgl_error_t xgl_transport_send(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                               const xgl_tx_data_t* tx_data);

/**
 * \brief           Receive and process packet from network layer
 * \param[in]       ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Packet from network layer (contains metadata)
 * \return          XGL_OK on success, error code otherwise
 * \note            Processes ACKs and delivers DATA to
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
 * \return          First hard maintenance error, otherwise the first immediate
 *                  feedback error, or XGL_OK
 * \note            Processes deferred sends, delivery and timeouts. Retained
 *                  BUSY/NO_MEMORY work remains owned and is retried later.
 * \note            Service receive events and xgl_transport_next_timeout()
 *                  deadlines; a zero delay requires maintenance now.
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
xgl_error_t xgl_transport_get_rx_endpoint(xgl_transport_ctx_t* ctx,
                                          xgl_packet_rx_endpoint_t* iface);

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_H */
