/**
 * \file            xgl_types.h
 * \brief           xgen-link Protocol Core Data Types and Structures
 * \author          X-Gen Lab
 */

#ifndef XGL_TYPES_H
#define XGL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xgen/memory/allocator.h>

#include "xgl/xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Forward Declarations                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Protocol instance handle (opaque pointer)
 */
typedef struct xgl_instance* xgl_handle_t;

/*---------------------------------------------------------------------------*/
/* Physical Layer Interface                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Physical layer operations
 * \details         Callbacks run during xgl_send_at() or xgl_step(). They
 *                  must be non-blocking or bounded, must not call back into the
 *                  same xgl_handle_t, and must return an xgl_error_t status.
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

/*---------------------------------------------------------------------------*/
/* Frame Header Structure                                                    */
/*---------------------------------------------------------------------------*/

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

/** Traffic-class bit position used to mark fragmented packets. */
#define XGL_TRAFFIC_FRAGMENTED_SHIFT 5

/** Traffic-class mask used to mark fragmented packets. */
#define XGL_TRAFFIC_FRAGMENTED_MASK 0x20

/** Reserved encryption-class bit position. */
#define XGL_TRAFFIC_ENCRYPTION_SHIFT 3

/** Reserved encryption-class mask. */
#define XGL_TRAFFIC_ENCRYPTION_MASK 0x18

/** No encryption. This is the only accepted production value today. */
#define XGL_TRAFFIC_ENCRYPTION_NONE 0x00

/** Reserved; rejected until the encryption path is wired. */
#define XGL_TRAFFIC_ENCRYPTION_AES128 0x08

/** Reserved; rejected until the encryption path is wired. */
#define XGL_TRAFFIC_ENCRYPTION_CHACHA20 0x10

/** Traffic-class bit position used for priority. */
#define XGL_TRAFFIC_PRIORITY_SHIFT 0

/** Traffic-class mask used for priority values 0..7. */
#define XGL_TRAFFIC_PRIORITY_MASK 0x07

/** Maximum authentication tag length accepted by public configuration. */
#define XGL_AUTH_TAG_MAX_LEN 32U

/**
 * \brief           Compression-class bits for negotiated payload handling
 */
#define XGL_COMPRESSION_SHIFT 6
/** Mask selecting the compression-class bits. */
#define XGL_COMPRESSION_MASK 0xC0

/** No compression. This is the only accepted production value today. */
#define XGL_COMPRESSION_NONE 0x00

/** Reserved; rejected until the codec path is wired. */
#define XGL_COMPRESSION_RLE 0x40

/** Reserved; rejected until the codec path is wired. */
#define XGL_COMPRESSION_LZ77 0x80

/** Reserved; rejected until the codec path is wired. */
#define XGL_COMPRESSION_ZLIB 0xC0

/*---------------------------------------------------------------------------*/
/* Route Table Entry                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Route table entry
 */
typedef struct {
    uint16_t target_id;      /**< Target node ID */
    xgl_phy_ops_t* phy;      /**< Physical layer operations */
    uint16_t max_frame_size; /**< Maximum frame size for this route */
    uint32_t read_freq_hz;   /**< Read frequency in Hz */
    uint8_t metric;          /**< Route metric (for dynamic routing) */
} xgl_route_item_t;

/*---------------------------------------------------------------------------*/
/* Callback Function Types                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Receive callback function type
 * \note            Runs synchronously; must not reenter, reset, or destroy the
 *                  instance. Data is borrowed only for the callback duration.
 * \param[in]       handle: Protocol instance handle
 * \param[in]       source_id: Source node ID
 * \param[in]       data_type: Data type
 * \param[in]       data: Received data buffer
 * \param[in]       len: Data length
 * \param[in]       user_data: User data
 */
typedef void (*xgl_rx_callback_t)(xgl_handle_t handle, uint16_t source_id,
                                  uint8_t data_type, const uint8_t* data,
                                  size_t len, void* user_data);

/**
 * \brief           Accept responsibility for a reliable application payload
 * \param[in]       handle: Delivering protocol instance
 * \param[in]       source_id: Remote sender identifier
 * \param[in]       data_type: Application payload type
 * \param[in]       data: Borrowed payload bytes
 * \param[in]       len: Number of payload bytes
 * \param[in]       user_data: Application callback context
 * \return          XGL_OK after consuming/copying; XGL_ERR_BUSY defers
 * acceptance
 * \note            Must not reenter, reset, or destroy the instance. Payload
 *                  bytes are borrowed only for the callback duration.
 */
typedef xgl_error_t (*xgl_rx_accept_fn)(xgl_handle_t handle, uint16_t source_id,
                                        uint8_t data_type, const uint8_t* data,
                                        size_t len, void* user_data);

/**
 * \brief           Per-link work limits for a nonblocking runtime step
 * \note            Zero RX bytes skips polling but still maintains transport.
 */
typedef struct {
    size_t rx_bytes; /**< Maximum PHY bytes polled per link during one step */
    uint32_t receive_timeout_ms; /**< Partial-frame timeout in milliseconds */
} xgl_work_budget_t;

/**
 * \brief           Error callback function type
 * \note            Runs synchronously; must not reenter, reset, or destroy the
 *                  instance. Queue recovery work for after the callback
 * returns.
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message string
 * \param[in]       user_data: User data
 */
typedef void (*xgl_error_callback_t)(xgl_handle_t handle, xgl_error_t error,
                                     const char* message, void* user_data);

/*---------------------------------------------------------------------------*/
/* Configuration Structure                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Memory configuration
 */
typedef struct {
    size_t rx_buffer_size; /**< RX buffer size in bytes */
    const xgm_allocator_t*
        allocator; /**< Explicit allocator; NULL uses the API creation policy */
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

/** Version of the provider input contract; independent from the wire version.
 */
#define XGL_AUTH_INPUT_VERSION 1U
/** Canonical directional nonce length in bytes. */
#define XGL_AUTH_NONCE_SIZE 12U

/**
 * \brief           Trusted bidirectional association installed by the
 * application
 * \note            Prefixes must be unique for each direction under a key.
 *                  The application must establish freshness across restarts;
 *                  reinstalling fixed parameters after RAM loss is not secure.
 */
typedef struct {
    uint16_t remote_id;       /**< Trusted remote node identifier */
    uint32_t connection_id;   /**< Application association identifier */
    uint32_t session_epoch;   /**< Trusted association generation */
    uint32_t tx_key_id;       /**< Provider key identity for outgoing frames */
    uint32_t rx_key_id;       /**< Provider key identity for incoming frames */
    uint32_t tx_nonce_prefix; /**< Unique outgoing nonce domain under its key */
    uint32_t rx_nonce_prefix; /**< Unique incoming nonce domain under its key */
    uint64_t tx_initial_seq;  /**< First outgoing security sequence */
    uint64_t rx_min_seq; /**< Lowest acceptable incoming security sequence */
} xgl_security_session_config_t;

/**
 * \brief           Versioned authentication vector derived from actual wire
 * bytes
 * \note            Nonce is BE32(installed directional prefix) followed by
 *                  BE64(security_seq). AAD contains the complete header/TLVs
 * with TTL and header CRC bytes zeroed. Providers must authenticate every AAD
 * and payload byte using this nonce and key identity. All pointers are borrowed
 * for the callback only. Providers are synchronous and must not reenter the
 * protocol instance.
 */
typedef struct {
    uint32_t version;       /**< Provider input contract version */
    uint8_t wire_version;   /**< Encoded protocol version */
    uint16_t source_id;     /**< Encoded sender identifier */
    uint16_t target_id;     /**< Encoded recipient identifier */
    uint32_t connection_id; /**< Encoded association identifier */
    uint32_t session_epoch; /**< Encoded trusted association generation */
    uint32_t key_id;        /**< Installed directional provider key identity */
    uint64_t security_seq;  /**< Per-direction sequence used in the nonce */
    uint8_t nonce[XGL_AUTH_NONCE_SIZE]; /**< Canonical authentication nonce */
    const uint8_t* aad; /**< Borrowed canonical header and extension bytes */
    size_t aad_len;     /**< Number of authenticated header bytes */
    const uint8_t* payload; /**< Borrowed payload bytes */
    size_t payload_len;     /**< Number of authenticated payload bytes */
} xgl_auth_input_t;

/**
 * \brief           Sign the complete versioned authentication vector
 * \param[in]       input: Validated, canonical protocol input
 * \param[out]      tag: Tag output buffer, borrowed for this call
 * \param[in]       tag_capacity: Available tag bytes
 * \param[out]      tag_len: Actual tag length; must equal provider.tag_len
 * \param[in]       user_data: Application provider context
 * \return          XGL_OK on success, error code otherwise
 * \note            The reserved security sequence is consumed even on failure.
 */
typedef xgl_error_t (*xgl_auth_sign_fn)(const xgl_auth_input_t* input,
                                        uint8_t* tag, size_t tag_capacity,
                                        size_t* tag_len, void* user_data);

/**
 * \brief           Verify a tag against the complete authentication vector
 * \param[in]       input: Validated, canonical protocol input
 * \param[in]       tag: Borrowed received tag bytes
 * \param[in]       tag_len: Expected fixed tag length
 * \param[out]      valid: True only after successful cryptographic verification
 * \param[in]       user_data: Application provider context
 * \return          XGL_OK when verification completed, error code otherwise
 */
typedef xgl_error_t (*xgl_auth_verify_fn)(const xgl_auth_input_t* input,
                                          const uint8_t* tag, size_t tag_len,
                                          bool* valid, void* user_data);

/** \brief           Synchronous application-supplied authentication algorithm
 */
typedef struct {
    xgl_auth_sign_fn sign; /**< Synchronous outgoing authentication callback */
    xgl_auth_verify_fn
        verify;      /**< Synchronous incoming verification callback */
    size_t tag_len;  /**< Fixed authentication tag size in bytes */
    void* user_data; /**< Borrowed application context passed to callbacks */
} xgl_auth_provider_t;

/**
 * \brief           Feature flags configuration
 */
typedef struct {
    bool enable_fragmentation; /**< Enable packet fragmentation */
    bool
        enable_compression; /**< Reserved; rejected until codec path is wired */
    bool enable_encryption; /**< Reserved; rejected until codec path is wired */
    size_t max_tx_packets;  /**< Total retained reliable packets */
    size_t max_rx_buffered_packets; /**< Total retained out-of-order packets */
    size_t max_reassembly_bytes;   /**< Shared incomplete/completed RX budget */
    size_t max_tx_message_bytes;   /**< Shared pending message copy budget */
    uint32_t peer_idle_timeout_ms; /**< Idle peer reclaim timeout; 0 = disabled
                                      (default 60000) */
    uint8_t
        max_reassembly_slots; /**< Max concurrent fragment reassembly slots */
    uint16_t max_peers;       /**< Peer/scope capacity; must be nonzero */
    size_t max_message_size;  /**< Per-message TX/RX byte limit when
                                 fragmentation is enabled */
} xgl_feature_config_t;

/**
 * \brief           Protocol configuration structure
 */
typedef struct {
    /*-----------------------------------------------------------------------*/
    /* Instance Identification                                               */
    /*-----------------------------------------------------------------------*/
    const char* name;   /**< Instance name (for debugging) */
    uint16_t source_id; /**< Local node ID */

    /*-----------------------------------------------------------------------*/
    /* Grouped Configuration                                                 */
    /*-----------------------------------------------------------------------*/
    xgl_memory_config_t memory;     /**< Memory configuration */
    xgl_protocol_config_t protocol; /**< Protocol parameters */
    xgl_feature_config_t features;  /**< Feature flags */

    /*-----------------------------------------------------------------------*/
    /* Authentication Configuration                                          */
    /*-----------------------------------------------------------------------*/
    bool auth_required; /**< Require authentication for packets */
    xgl_auth_provider_t* auth_provider; /**< Authentication callback provider */

    /*-----------------------------------------------------------------------*/
    /* Routing Configuration                                                 */
    /*-----------------------------------------------------------------------*/
    xgl_route_item_t* route_table; /**< Route table array */
    size_t route_table_len;        /**< Number of routes in table */

    /*-----------------------------------------------------------------------*/
    /* Callbacks                                                             */
    /*-----------------------------------------------------------------------*/
    xgl_rx_callback_t rx_callback; /**< Receive callback */
    xgl_rx_accept_fn
        rx_accept_callback; /**< Optional bounded application admission */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;            /**< User data for callbacks */
} xgl_config_t;

/*---------------------------------------------------------------------------*/
/* Statistics Structure                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Layer-specific statistics structure
 * \details         Each protocol layer maintains its own statistics to avoid
 *                  double-counting packets as they traverse the stack
 */
typedef struct {
    /*-----------------------------------------------------------------------*/
    /* Transmission Statistics                                               */
    /*-----------------------------------------------------------------------*/
    uint64_t tx_packets; /**< Total transmitted packets */
    uint64_t tx_bytes;   /**< Total transmitted bytes */
    uint64_t tx_errors;  /**< Transmission errors */

    /*-----------------------------------------------------------------------*/
    /* Reception Statistics                                                  */
    /*-----------------------------------------------------------------------*/
    uint64_t rx_packets; /**< Total received packets */
    uint64_t rx_bytes;   /**< Total received bytes */
    uint64_t rx_errors;  /**< Reception errors */
    uint64_t rx_dropped; /**< Dropped packets */
} xgl_layer_stats_t;

/**
 * \brief           Counters read and reset under caller-serialized access
 */
typedef struct {
    /*-----------------------------------------------------------------------*/
    /* Layer-Specific Statistics                                             */
    /*-----------------------------------------------------------------------*/
    xgl_layer_stats_t datalink;  /**< Data link layer statistics */
    xgl_layer_stats_t network;   /**< Network layer statistics */
    xgl_layer_stats_t transport; /**< Transport layer statistics */

    /*-----------------------------------------------------------------------*/
    /* Protocol-Specific Counters                                            */
    /*-----------------------------------------------------------------------*/
    uint64_t tx_retries;           /**< Retransmission count (transport) */
    uint64_t rx_header_crc_errors; /**< Header CRC errors (datalink) */
    uint64_t rx_crc16_errors;      /**< Frame CRC16 errors (datalink) */

    /*-----------------------------------------------------------------------*/
    /* Performance Metrics                                                   */
    /*-----------------------------------------------------------------------*/
    uint32_t avg_rtt_ms; /**< Average RTT in milliseconds */
    uint32_t max_rtt_ms; /**< Maximum RTT in milliseconds */
    uint32_t min_rtt_ms; /**< Minimum RTT in milliseconds */

    /*-----------------------------------------------------------------------*/
    /* Memory Usage                                                          */
    /*-----------------------------------------------------------------------*/
    size_t memory_used; /**< Current memory usage in bytes */
    size_t memory_peak; /**< Peak memory usage in bytes */
} xgl_statistics_t;

/*---------------------------------------------------------------------------*/
/* Transmission Data Structures                                              */
/*---------------------------------------------------------------------------*/

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
    uint8_t priority;    /**< Priority level (0-7) */
    uint32_t timeout_ms; /**< ACK timeout in ms (0 = default/RTT-derived) */
    uint32_t
        connection_id; /**< Connection scope for peer state (0 = default) */
    uint32_t session_epoch; /**< Session epoch for replay/peer/fragment
                               isolation (0 = default) */
    uint8_t compression_id; /**< Compression codec id (0 = none); maps to codec
                               registry */
} xgl_tx_data_t;

/**
 * \brief           Zero-copy transmission data
 */
typedef struct {
    uint8_t* buffer;    /**< Buffer with pre-allocated header space */
    size_t buffer_size; /**< Total buffer size */
    size_t data_offset; /**< Data start offset; add XGL_DATA_TYPE_EXT_SIZE when
                           data_type is non-zero */
    size_t data_len;    /**< Actual data length */

    /* Transmission parameters */
    uint16_t target_id;  /**< Target node ID */
    uint8_t data_type;   /**< Application payload class encoded as DATA_TYPE_EXT
                            when non-zero */
    bool reliable;       /**< Enable reliable transmission */
    uint8_t priority;    /**< Priority level (0-7) */
    uint32_t timeout_ms; /**< Timeout in ms (0 = use default) */
} xgl_tx_data_zerocopy_t;

/*---------------------------------------------------------------------------*/
/* Configuration Presets                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Tiny configuration preset
 */
#define XGL_CONFIG_PRESET_TINY                                                 \
    {                                                                          \
        /* name */ "tiny",                                                     \
        /* source_id */ 1, /* memory */                                        \
        {                                                                      \
            /* rx_buffer_size */ 160,                                          \
            /* allocator */ NULL,                                              \
        }, /* protocol */                                                      \
        {                                                                      \
            /* ack_timeout_ms */ 1000,                                         \
            /* max_retry_count */ 3,                                           \
            /* window_size */ 2,                                               \
            /* max_frame_size */ 128,                                          \
        }, /* features */                                                      \
        {                                                                      \
            /* enable_fragmentation */ false,                                  \
            /* enable_compression */ false,                                    \
            /* enable_encryption */ false,                                     \
            /* max_tx_packets */ 2,                                            \
            /* max_rx_buffered_packets */ 2,                                   \
            /* max_reassembly_bytes */ 0,                                      \
            /* max_tx_message_bytes */ 0,                                      \
            /* peer_idle_timeout_ms */ 60000,                                  \
            /* max_reassembly_slots */ 2,                                      \
            /* max_peers */ 1,                                                 \
            /* max_message_size */ 0,                                          \
        },                                                                     \
        /* auth_required */ false,                                             \
        /* auth_provider */ NULL,                                              \
        /* route_table */ NULL,                                                \
        /* route_table_len */ 0,                                               \
        /* rx_callback */ NULL,                                                \
        /* rx_accept_callback */ NULL,                                         \
        /* error_callback */ NULL,                                             \
        /* callback_user_data */ NULL,                                         \
    }

/**
 * \brief           Small configuration preset
 */
#define XGL_CONFIG_PRESET_SMALL                                                \
    {                                                                          \
        /* name */ "small",                                                    \
        /* source_id */ 1, /* memory */                                        \
        {                                                                      \
            /* rx_buffer_size */ 288,                                          \
            /* allocator */ NULL,                                              \
        }, /* protocol */                                                      \
        {                                                                      \
            /* ack_timeout_ms */ 1000,                                         \
            /* max_retry_count */ 5,                                           \
            /* window_size */ 4,                                               \
            /* max_frame_size */ 256,                                          \
        }, /* features */                                                      \
        {                                                                      \
            /* enable_fragmentation */ true,                                   \
            /* enable_compression */ false,                                    \
            /* enable_encryption */ false,                                     \
            /* max_tx_packets */ 4,                                            \
            /* max_rx_buffered_packets */ 4,                                   \
            /* max_reassembly_bytes */ 2048,                                   \
            /* max_tx_message_bytes */ 2048,                                   \
            /* peer_idle_timeout_ms */ 60000,                                  \
            /* max_reassembly_slots */ 4,                                      \
            /* max_peers */ 2,                                                 \
            /* max_message_size */ 1024,                                       \
        },                                                                     \
        /* auth_required */ false,                                             \
        /* auth_provider */ NULL,                                              \
        /* route_table */ NULL,                                                \
        /* route_table_len */ 0,                                               \
        /* rx_callback */ NULL,                                                \
        /* rx_accept_callback */ NULL,                                         \
        /* error_callback */ NULL,                                             \
        /* callback_user_data */ NULL,                                         \
    }

/**
 * \brief           Medium configuration preset
 */
#define XGL_CONFIG_PRESET_MEDIUM                                               \
    {                                                                          \
        /* name */ "medium",                                                   \
        /* source_id */ 1, /* memory */                                        \
        {                                                                      \
            /* rx_buffer_size */ 544,                                          \
            /* allocator */ NULL,                                              \
        }, /* protocol */                                                      \
        {                                                                      \
            /* ack_timeout_ms */ 1000,                                         \
            /* max_retry_count */ 5,                                           \
            /* window_size */ 8,                                               \
            /* max_frame_size */ 512,                                          \
        }, /* features */                                                      \
        {                                                                      \
            /* enable_fragmentation */ true,                                   \
            /* enable_compression */ false,                                    \
            /* enable_encryption */ false,                                     \
            /* max_tx_packets */ 16,                                           \
            /* max_rx_buffered_packets */ 16,                                  \
            /* max_reassembly_bytes */ 16384,                                  \
            /* max_tx_message_bytes */ 16384,                                  \
            /* peer_idle_timeout_ms */ 60000,                                  \
            /* max_reassembly_slots */ 8,                                      \
            /* max_peers */ 8,                                                 \
            /* max_message_size */ 4096,                                       \
        },                                                                     \
        /* auth_required */ false,                                             \
        /* auth_provider */ NULL,                                              \
        /* route_table */ NULL,                                                \
        /* route_table_len */ 0,                                               \
        /* rx_callback */ NULL,                                                \
        /* rx_accept_callback */ NULL,                                         \
        /* error_callback */ NULL,                                             \
        /* callback_user_data */ NULL,                                         \
    }

/**
 * \brief           Large configuration preset
 */
#define XGL_CONFIG_PRESET_LARGE                                                \
    {                                                                          \
        /* name */ "large",                                                    \
        /* source_id */ 1, /* memory */                                        \
        {                                                                      \
            /* rx_buffer_size */ 1056,                                         \
            /* allocator */ NULL,                                              \
        }, /* protocol */                                                      \
        {                                                                      \
            /* ack_timeout_ms */ 1000,                                         \
            /* max_retry_count */ 7,                                           \
            /* window_size */ 16,                                              \
            /* max_frame_size */ 1024,                                         \
        }, /* features */                                                      \
        {                                                                      \
            /* enable_fragmentation */ true,                                   \
            /* enable_compression */ false,                                    \
            /* enable_encryption */ false,                                     \
            /* max_tx_packets */ 64,                                           \
            /* max_rx_buffered_packets */ 64,                                  \
            /* max_reassembly_bytes */ 131072,                                 \
            /* max_tx_message_bytes */ 131072,                                 \
            /* peer_idle_timeout_ms */ 60000,                                  \
            /* max_reassembly_slots */ 16,                                     \
            /* max_peers */ 16,                                                \
            /* max_message_size */ 16384,                                      \
        },                                                                     \
        /* auth_required */ false,                                             \
        /* auth_provider */ NULL,                                              \
        /* route_table */ NULL,                                                \
        /* route_table_len */ 0,                                               \
        /* rx_callback */ NULL,                                                \
        /* rx_accept_callback */ NULL,                                         \
        /* error_callback */ NULL,                                             \
        /* callback_user_data */ NULL,                                         \
    }

/**
 * \brief           Production configuration preset
 * \details         Production profile requires authentication by default.
 *                  The application must provide auth_provider before
 * validate/create.
 */
#define XGL_CONFIG_PRESET_PRODUCTION                                           \
    {                                                                          \
        /* name */ "production",                                               \
        /* source_id */ 1, /* memory */                                        \
        {                                                                      \
            /* rx_buffer_size */ 1056,                                         \
            /* allocator */ NULL,                                              \
        }, /* protocol */                                                      \
        {                                                                      \
            /* ack_timeout_ms */ 1000,                                         \
            /* max_retry_count */ 7,                                           \
            /* window_size */ 16,                                              \
            /* max_frame_size */ 1024,                                         \
        }, /* features */                                                      \
        {                                                                      \
            /* enable_fragmentation */ true,                                   \
            /* enable_compression */ false,                                    \
            /* enable_encryption */ false,                                     \
            /* max_tx_packets */ 64,                                           \
            /* max_rx_buffered_packets */ 64,                                  \
            /* max_reassembly_bytes */ 131072,                                 \
            /* max_tx_message_bytes */ 131072,                                 \
            /* peer_idle_timeout_ms */ 60000,                                  \
            /* max_reassembly_slots */ 16,                                     \
            /* max_peers */ 16,                                                \
            /* max_message_size */ 16384,                                      \
        },                                                                     \
        /* auth_required */ true,                                              \
        /* auth_provider */ NULL,                                              \
        /* route_table */ NULL,                                                \
        /* route_table_len */ 0,                                               \
        /* rx_callback */ NULL,                                                \
        /* rx_accept_callback */ NULL,                                         \
        /* error_callback */ NULL,                                             \
        /* callback_user_data */ NULL,                                         \
    }

#ifdef __cplusplus
}
#endif

#endif /* XGL_TYPES_H */
