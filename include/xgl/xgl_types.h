/**
 * \file            xgl_types.h
 * \brief           Core protocol configuration and buffer contracts
 * \author          X-Gen Lab
 */

#ifndef XGL_TYPES_H
#define XGL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <cstddef>
#endif

#include <xgl/xgl_config.h>

#include "xgl/xgl_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief           Opaque protocol instance handle. */
typedef struct xgl_instance* xgl_handle_t;

/** \brief           Authentication provider declared in xgl_security.h. */
typedef struct xgl_auth_provider xgl_auth_provider_t;

/** \brief           Runtime policy declared in xgl_runtime.h. */
typedef struct xgl_instance_options xgl_instance_options_t;

/**
 * \brief           Physical layer operations
 * \details         Callbacks run synchronously during send and runtime calls.
 * They must be non-blocking or bounded, must not call back into the same
 * xgl_handle_t, and must return an xgl_error_t status.
 */
typedef struct {
    /**
     * \brief       Transmit one complete serialized frame.
     * \param[in]   data: Frame bytes to transmit
     * \param[in]   len: Number of bytes in data
     * \param[in]   user_data: PHY user data pointer
     * \return      XGL_OK after consuming or copying all frame bytes
     * \note        No pointer may be retained after return, including DMA.
     */
    xgl_error_t (*tx)(const uint8_t* data, size_t len, void* user_data);

    /**
     * \brief       Receive available serialized frame bytes.
     * \param[out]  buffer: Destination buffer owned by xgen-link
     * \param[in,out] len: On entry, buffer capacity; on return, bytes written
     * \param[in]   user_data: PHY user data pointer
     * \return      XGL_OK when the read completed; zero bytes is allowed
     */
    xgl_error_t (*rx)(uint8_t* buffer, size_t* len, void* user_data);
    void* user_data; /**< User data for PHY operations */
} xgl_phy_ops_t;

/**
 * \brief           CRC16 size in bytes
 */
#define XGL_CRC16_SIZE 2

/**
 * \brief           Production fixed wire header size in bytes
 */
#define XGL_FRAME_HEADER_SIZE 24

/**
 * \brief           Header TLV bytes required when data_type is non-zero
 */
#define XGL_DATA_TYPE_EXT_SIZE 3U

/*---------------------------------------------------------------------------*/
/* Production Traffic-Class Bit Definitions                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Traffic-class reliability bits
 */
#define XGL_RELIABILITY_CLASS_SHIFT 6
/** Mask selecting the traffic-class reliability bits. */
#define XGL_RELIABILITY_CLASS_MASK 0xC0

/** No transport acknowledgement is requested. */
#define XGL_RELIABILITY_NONE 0x00

/** Packet elicits ACK range/SACK feedback and may be retransmitted. */
#define XGL_RELIABILITY_ACK_ELICITING 0x40

/** Packet carries acknowledgement information and no application payload. */
#define XGL_RELIABILITY_ACK_ONLY 0x80

/** Traffic-class bit position used for priority. */
#define XGL_TRAFFIC_PRIORITY_SHIFT 0

/** Traffic-class mask used for priority values 0..7. */
#define XGL_TRAFFIC_PRIORITY_MASK 0x07

/** Maximum authentication tag length accepted by public configuration. */
#define XGL_AUTH_TAG_MAX_LEN 32U

/**
 * \brief           Route table entry
 * \details         Static configuration has one entry per nonzero remote
 *                  destination; broadcast is allowed. Entries sharing a PHY
 *                  contribute initialization hints to one receive link, whose
 *                  polling uses their fastest frequency. Runtime route changes
 *                  do not register or reschedule receive links.
 */
typedef struct {
    uint16_t target_id;      /**< Target node ID */
    xgl_phy_ops_t* phy;      /**< Physical layer operations */
    uint16_t max_frame_size; /**< Maximum frame size for this route */
    uint32_t read_freq_hz;   /**< Positive initialization polling hint in Hz */
} xgl_route_item_t;

/** \brief           Exact identity and semantics of synchronously borrowed RX.
 */
typedef struct {
    uint16_t source_id;     /**< Remote endpoint */
    uint32_t connection_id; /**< Connection identity */
    uint32_t session_epoch; /**< Connection generation */
    uint8_t data_type;      /**< Application payload classification */
    bool reliable;          /**< Protocol owns acceptance until delivery */
} xgl_rx_info_t;

/**
 * \brief           Accept a payload with its complete connection identity
 * \param[in]       handle: Serialized instance, never reenter from this call
 * \param[in]       info: Borrowed receive metadata
 * \param[in]       data: Borrowed payload valid only during this call
 * \param[in]       length: Payload bytes
 * \param[in]       user_data: Application context
 * \return          XGL_OK when accepted; BUSY/NO_MEMORY defer acceptance.
 *                  Reliable input already retained for reordering stays owned
 *                  by the instance. Other reliable input relies on sender
 * retry.
 */
typedef xgl_error_t (*xgl_rx_scoped_fn)(xgl_handle_t handle,
                                        const xgl_rx_info_t* info,
                                        const uint8_t* data, size_t length,
                                        void* user_data);

/**
 * \brief           Error callback function type
 * \note            Runs synchronously; must not reenter, reset, or destroy the
 *                  instance. Queue recovery work for after the callback
 * returns.
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Non-NULL text; empty when diagnostics are disabled
 * \param[in]       user_data: User data
 */
typedef void (*xgl_error_callback_t)(xgl_handle_t handle, xgl_error_t error,
                                     const char* message, void* user_data);

/**
 * \brief           Per-link work limits for a nonblocking runtime step
 * \note            Zero RX bytes skips polling but still maintains transport.
 */
typedef struct {
    size_t rx_bytes; /**< Maximum PHY bytes polled per link during one step */
    uint32_t receive_timeout_ms; /**< Partial-frame timeout in milliseconds */
} xgl_work_budget_t;

/** \brief           Storage type aligned for the target's scalar types. */
#if defined(__cplusplus)
typedef std::max_align_t xgl_max_align_t;
#elif defined(_MSC_VER)
/* The MSVC C11 headers omit max_align_t. */
typedef union {
    long double floating; /**< Maximum floating-point alignment. */
    long long integer;    /**< Maximum integer alignment. */
    void* pointer;        /**< Object pointer alignment. */
} xgl_max_align_t;
#else
typedef max_align_t xgl_max_align_t;
#endif

/**
 * \brief           Memory configuration
 */
typedef struct {
    size_t rx_buffer_size; /**< RX buffer size in bytes */
} xgl_memory_config_t;

/**
 * \brief           Protocol parameters configuration
 */
typedef struct {
    uint32_t ack_timeout_ms; /**< ACK timeout in milliseconds */
    uint8_t max_retry_count; /**< Maximum retry count */
    uint8_t window_size;     /**< Sliding window size */
    uint16_t max_frame_size; /**< Maximum frame size in bytes */
} xgl_protocol_config_t;

/**
 * \brief           Reliable peer and packet capacities
 */
typedef struct {
    size_t max_tx_packets;          /**< Total retained reliable packets */
    size_t max_rx_buffered_packets; /**< Total retained out-of-order packets */
    uint32_t
        peer_idle_timeout_ms; /**< Idle peer reclaim timeout; 0 = disabled */
    uint16_t max_peers;       /**< Peer/scope capacity; must be nonzero */
} xgl_feature_config_t;

/**
 * \brief           Immutable instance configuration borrowed until destroy
 */
typedef struct {
    uint16_t source_id;             /**< Local node identifier */
    xgl_memory_config_t memory;     /**< Per-PHY RX storage */
    xgl_protocol_config_t protocol; /**< Frame, retry and window limits */
    xgl_feature_config_t features;  /**< Reliable resource capacities */
    bool auth_required;             /**< Require local authentication */
    const xgl_auth_provider_t* auth_provider; /**< Optional borrowed provider */
    xgl_route_item_t* route_table;       /**< Immutable borrowed route array */
    size_t route_table_len;              /**< Number of route entries */
    xgl_rx_scoped_fn rx_callback;        /**< Synchronous payload acceptance */
    void* rx_user_data;                  /**< Context for rx_callback */
    xgl_error_callback_t error_callback; /**< Synchronous error observer */
    void* callback_user_data;            /**< Context for error_callback */
    const xgl_instance_options_t* options; /**< Optional runtime policy */
} xgl_config_t;

/**
 * \brief           Standard transmission data
 */
typedef struct {
    uint16_t target_id;  /**< Target node ID */
    uint8_t data_type;   /**< Application payload class encoded as DATA_TYPE_EXT
                            when non-zero */
    const uint8_t* data; /**< Data buffer */
    size_t data_len;     /**< Data length */
    bool reliable;       /**< Enable reliable transmission */
    uint8_t priority; /**< Wire priority (0-7); no local priority scheduling */
    uint32_t timeout_ms; /**< Initial packet ACK timeout in ms; 0 selects
                            RTT/config. */
    uint32_t
        connection_id; /**< Connection scope for peer state (0 = default) */
    uint32_t session_epoch; /**< Session epoch for replay and peer isolation
                               (0 = default) */
} xgl_tx_data_t;

#ifdef __cplusplus
}
#endif

#endif /* XGL_TYPES_H */
