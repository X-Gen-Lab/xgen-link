/**
 * \file            xgl_types.h
 * \brief           xgen-link Protocol Core Data Types and Structures
 * \author          Nexus Team
 */

#ifndef XGL_TYPES_H
#define XGL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Forward Declarations                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Protocol instance handle (opaque pointer)
 */
typedef struct xgl_instance* xgl_handle_t;

/*---------------------------------------------------------------------------*/
/* Memory Allocator Interface                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Memory allocator interface
 */
typedef struct {
    void* (*malloc)(size_t size);   /**< Allocation function */
    void (*free)(void* ptr);        /**< Deallocation function */
    void* user_data;                /**< User data for allocator */
} xgl_allocator_t;

/*---------------------------------------------------------------------------*/
/* Physical Layer Interface                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Physical layer operations
 */
typedef struct {
    xgl_error_t (*tx)(const uint8_t* data, size_t len, void* user_data);
    xgl_error_t (*rx)(uint8_t* buffer, size_t* len, void* user_data);
    void* user_data;                /**< User data for PHY operations */
} xgl_phy_ops_t;

/*---------------------------------------------------------------------------*/
/* Frame Header Structure                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           CRC16 size in bytes
 */
#define XGL_CRC16_SIZE              2

/**
 * \brief           Production fixed wire header size in bytes
 */
#define XGL_FRAME_HEADER_SIZE       24

/*---------------------------------------------------------------------------*/
/* Production Traffic-Class Bit Definitions                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Traffic-class reliability bits
 */
#define XGL_RELIABILITY_CLASS_SHIFT     6
#define XGL_RELIABILITY_CLASS_MASK      0xC0
#define XGL_RELIABILITY_NONE            0x00
#define XGL_RELIABILITY_ACK_ELICITING   0x40
#define XGL_RELIABILITY_ACK_ONLY        0x80

#define XGL_TRAFFIC_FRAGMENTED_SHIFT    5
#define XGL_TRAFFIC_FRAGMENTED_MASK     0x20

#define XGL_TRAFFIC_ENCRYPTION_SHIFT    3
#define XGL_TRAFFIC_ENCRYPTION_MASK     0x18
#define XGL_TRAFFIC_ENCRYPTION_NONE     0x00
#define XGL_TRAFFIC_ENCRYPTION_AES128   0x08
#define XGL_TRAFFIC_ENCRYPTION_CHACHA20 0x10

#define XGL_TRAFFIC_PRIORITY_SHIFT      0
#define XGL_TRAFFIC_PRIORITY_MASK       0x07

/**
 * \brief           Compression-class bits for negotiated payload handling
 */
#define XGL_COMPRESSION_SHIFT           6
#define XGL_COMPRESSION_MASK            0xC0
#define XGL_COMPRESSION_NONE            0x00
#define XGL_COMPRESSION_RLE             0x40
#define XGL_COMPRESSION_LZ77            0x80
#define XGL_COMPRESSION_ZLIB            0xC0

#define XGL_SESSION_ID_MASK             0x3F

/*---------------------------------------------------------------------------*/
/* Packet Data Structure                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Packet data with reference counting
 */
typedef struct {
    uint32_t ref_count;             /**< Reference count (atomic if thread-safe) */
    size_t data_len;                /**< Data length in bytes */
    const uint8_t* data;            /**< Pointer to packet data */
    uint8_t* owned_data;            /**< Owned data buffer, NULL for borrowed data */
} xgl_packet_data_t;

/*---------------------------------------------------------------------------*/
/* Route Table Entry                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Route table entry
 */
typedef struct {
    uint16_t target_id;             /**< Target node ID */
    xgl_phy_ops_t* phy;             /**< Physical layer operations */
    uint16_t max_frame_size;        /**< Maximum frame size for this route */
    uint32_t read_freq_hz;          /**< Read frequency in Hz */
    uint8_t metric;                 /**< Route metric (for dynamic routing) */
} xgl_route_item_t;

/*---------------------------------------------------------------------------*/
/* Callback Function Types                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Receive callback function type
 * \param[in]       handle: Protocol instance handle
 * \param[in]       source_id: Source node ID
 * \param[in]       data_type: Data type
 * \param[in]       data: Received data buffer
 * \param[in]       len: Data length
 * \param[in]       user_data: User data
 */
typedef void (*xgl_rx_callback_t)(xgl_handle_t handle,
                                  uint16_t source_id,
                                  uint8_t data_type,
                                  const uint8_t* data,
                                  size_t len,
                                  void* user_data);

/**
 * \brief           Error callback function type
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message string
 * \param[in]       user_data: User data
 */
typedef void (*xgl_error_callback_t)(xgl_handle_t handle,
                                     xgl_error_t error,
                                     const char* message,
                                     void* user_data);

/*---------------------------------------------------------------------------*/
/* Configuration Structure                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Memory configuration
 */
typedef struct {
    size_t tx_pool_size;            /**< TX memory pool size in bytes */
    size_t rx_buffer_size;          /**< RX buffer size in bytes */
    xgl_allocator_t* allocator;     /**< Custom allocator (NULL = malloc/free) */
} xgl_memory_config_t;

/**
 * \brief           Protocol parameters configuration
 */
typedef struct {
    uint32_t ack_timeout_ms;        /**< ACK timeout in milliseconds */
    uint8_t max_retry_count;        /**< Maximum retry count */
    uint8_t window_size;            /**< Sliding window size */
    uint16_t max_frame_size;        /**< Maximum frame size in bytes */
} xgl_protocol_config_t;

typedef xgl_error_t (*xgl_auth_sign_fn)(uint32_t key_id,
                                        const uint8_t* aad,
                                        size_t aad_len,
                                        const uint8_t* payload,
                                        size_t payload_len,
                                        uint8_t* tag,
                                        size_t tag_capacity,
                                        size_t* tag_len,
                                        void* user_data);

typedef xgl_error_t (*xgl_auth_verify_fn)(uint32_t key_id,
                                          const uint8_t* aad,
                                          size_t aad_len,
                                          const uint8_t* payload,
                                          size_t payload_len,
                                          const uint8_t* tag,
                                          size_t tag_len,
                                          bool* valid,
                                          void* user_data);

typedef struct {
    xgl_auth_sign_fn sign;          /**< Generate authentication tag */
    xgl_auth_verify_fn verify;      /**< Verify authentication tag */
    void* user_data;                /**< Provider user data */
} xgl_auth_provider_t;

/**
 * \brief           Feature flags configuration
 */
typedef struct {
    bool enable_fragmentation;      /**< Enable packet fragmentation */
    bool enable_compression;        /**< Reserved; rejected until codec path is wired */
    bool enable_encryption;         /**< Reserved; rejected until codec path is wired */
    bool thread_safe;               /**< Enable thread safety when built with XGL_THREAD_SAFE */
} xgl_feature_config_t;

/**
 * \brief           Protocol configuration structure
 */
typedef struct {
    /*-----------------------------------------------------------------------*/
    /* Instance Identification                                               */
    /*-----------------------------------------------------------------------*/
    const char* name;               /**< Instance name (for debugging) */
    uint16_t source_id;             /**< Local node ID */
    
    /*-----------------------------------------------------------------------*/
    /* Grouped Configuration                                                 */
    /*-----------------------------------------------------------------------*/
    xgl_memory_config_t memory;     /**< Memory configuration */
    xgl_protocol_config_t protocol; /**< Protocol parameters */
    xgl_feature_config_t features;  /**< Feature flags */

    /*-----------------------------------------------------------------------*/
    /* Authentication Configuration                                          */
    /*-----------------------------------------------------------------------*/
    bool auth_required;             /**< Require authentication for packets */
    uint32_t auth_key_id;           /**< Active authentication key id */
    xgl_auth_provider_t* auth_provider; /**< Authentication callback provider */
    
    /*-----------------------------------------------------------------------*/
    /* Routing Configuration                                                 */
    /*-----------------------------------------------------------------------*/
    xgl_route_item_t* route_table;  /**< Route table array */
    size_t route_table_len;         /**< Number of routes in table */
    
    /*-----------------------------------------------------------------------*/
    /* Callbacks                                                             */
    /*-----------------------------------------------------------------------*/
    xgl_rx_callback_t rx_callback;  /**< Receive callback */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;       /**< User data for callbacks */
    
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
    uint64_t tx_packets;            /**< Total transmitted packets */
    uint64_t tx_bytes;              /**< Total transmitted bytes */
    uint64_t tx_errors;             /**< Transmission errors */
    
    /*-----------------------------------------------------------------------*/
    /* Reception Statistics                                                  */
    /*-----------------------------------------------------------------------*/
    uint64_t rx_packets;            /**< Total received packets */
    uint64_t rx_bytes;              /**< Total received bytes */
    uint64_t rx_errors;             /**< Reception errors */
    uint64_t rx_dropped;            /**< Dropped packets */
} xgl_layer_stats_t;

/**
 * \brief           Protocol statistics structure
 * \details         Aggregated statistics across all protocol layers with
 *                  layer-specific counters to prevent double-counting
 */
typedef struct {
    /*-----------------------------------------------------------------------*/
    /* Layer-Specific Statistics                                             */
    /*-----------------------------------------------------------------------*/
    xgl_layer_stats_t datalink;     /**< Data link layer statistics */
    xgl_layer_stats_t network;      /**< Network layer statistics */
    xgl_layer_stats_t transport;    /**< Transport layer statistics */
    
    /*-----------------------------------------------------------------------*/
    /* Protocol-Specific Counters                                            */
    /*-----------------------------------------------------------------------*/
    uint64_t tx_retries;            /**< Retransmission count (transport) */
    uint64_t rx_header_crc_errors;  /**< Header CRC errors (datalink) */
    uint64_t rx_crc16_errors;       /**< Frame CRC16 errors (datalink) */
    
    /*-----------------------------------------------------------------------*/
    /* Performance Metrics                                                   */
    /*-----------------------------------------------------------------------*/
    uint32_t avg_rtt_ms;            /**< Average RTT in milliseconds */
    uint32_t max_rtt_ms;            /**< Maximum RTT in milliseconds */
    uint32_t min_rtt_ms;            /**< Minimum RTT in milliseconds */
    
    /*-----------------------------------------------------------------------*/
    /* Memory Usage                                                          */
    /*-----------------------------------------------------------------------*/
    size_t memory_used;             /**< Current memory usage in bytes */
    size_t memory_peak;             /**< Peak memory usage in bytes */
    
} xgl_statistics_t;

/*---------------------------------------------------------------------------*/
/* Transmission Data Structures                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Standard transmission data
 */
typedef struct {
    uint16_t target_id;             /**< Target node ID */
    uint8_t data_type;              /**< Data type */
    const uint8_t* data;            /**< Data buffer */
    size_t data_len;                /**< Data length */
    bool reliable;                  /**< Enable reliable transmission */
    uint8_t priority;               /**< Priority level (0-7) */
    uint32_t timeout_ms;            /**< Timeout in ms (0 = use default) */
} xgl_tx_data_t;

/**
 * \brief           Zero-copy transmission data
 */
typedef struct {
    uint8_t* buffer;                /**< Buffer with pre-allocated header space */
    size_t buffer_size;             /**< Total buffer size */
    size_t data_offset;             /**< Data start offset (= XGL_FRAME_HEADER_SIZE) */
    size_t data_len;                /**< Actual data length */
    
    /* Transmission parameters */
    uint16_t target_id;             /**< Target node ID */
    uint8_t data_type;              /**< Data type */
    bool reliable;                  /**< Enable reliable transmission */
    uint8_t priority;               /**< Priority level (0-7) */
    uint32_t timeout_ms;            /**< Timeout in ms (0 = use default) */
} xgl_tx_data_zerocopy_t;

/*---------------------------------------------------------------------------*/
/* Configuration Presets                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Tiny configuration preset (32KB RAM, 50KB Flash)
 */
#define XGL_CONFIG_PRESET_TINY { \
    .name = "tiny", \
    .source_id = 1, \
    .memory = { \
        .tx_pool_size = 1024, \
        .rx_buffer_size = 160, \
        .allocator = NULL, \
    }, \
    .protocol = { \
        .ack_timeout_ms = 1000, \
        .max_retry_count = 3, \
        .window_size = 2, \
        .max_frame_size = 128, \
    }, \
    .features = { \
        .enable_fragmentation = false, \
        .enable_compression = false, \
        .enable_encryption = false, \
        .thread_safe = false, \
    }, \
    .auth_required = false, \
    .auth_key_id = 0, \
    .auth_provider = NULL, \
    .route_table = NULL, \
    .route_table_len = 0, \
    .rx_callback = NULL, \
    .error_callback = NULL, \
    .callback_user_data = NULL, \
}

/**
 * \brief           Small configuration preset (64KB RAM, 100KB Flash)
 */
#define XGL_CONFIG_PRESET_SMALL { \
    .name = "small", \
    .source_id = 1, \
    .memory = { \
        .tx_pool_size = 2048, \
        .rx_buffer_size = 288, \
        .allocator = NULL, \
    }, \
    .protocol = { \
        .ack_timeout_ms = 1000, \
        .max_retry_count = 5, \
        .window_size = 4, \
        .max_frame_size = 256, \
    }, \
    .features = { \
        .enable_fragmentation = true, \
        .enable_compression = false, \
        .enable_encryption = false, \
        .thread_safe = false, \
    }, \
    .auth_required = false, \
    .auth_key_id = 0, \
    .auth_provider = NULL, \
    .route_table = NULL, \
    .route_table_len = 0, \
    .rx_callback = NULL, \
    .error_callback = NULL, \
    .callback_user_data = NULL, \
}

/**
 * \brief           Medium configuration preset (128KB RAM, 256KB Flash)
 */
#define XGL_CONFIG_PRESET_MEDIUM { \
    .name = "medium", \
    .source_id = 1, \
    .memory = { \
        .tx_pool_size = 4096, \
        .rx_buffer_size = 544, \
        .allocator = NULL, \
    }, \
    .protocol = { \
        .ack_timeout_ms = 1000, \
        .max_retry_count = 5, \
        .window_size = 8, \
        .max_frame_size = 512, \
    }, \
    .features = { \
        .enable_fragmentation = true, \
        .enable_compression = false, \
        .enable_encryption = false, \
        .thread_safe = false, \
    }, \
    .auth_required = false, \
    .auth_key_id = 0, \
    .auth_provider = NULL, \
    .route_table = NULL, \
    .route_table_len = 0, \
    .rx_callback = NULL, \
    .error_callback = NULL, \
    .callback_user_data = NULL, \
}

/**
 * \brief           Large configuration preset (256KB+ RAM, 512KB+ Flash)
 */
#define XGL_CONFIG_PRESET_LARGE { \
    .name = "large", \
    .source_id = 1, \
    .memory = { \
        .tx_pool_size = 8192, \
        .rx_buffer_size = 1056, \
        .allocator = NULL, \
    }, \
    .protocol = { \
        .ack_timeout_ms = 1000, \
        .max_retry_count = 7, \
        .window_size = 16, \
        .max_frame_size = 1024, \
    }, \
    .features = { \
        .enable_fragmentation = true, \
        .enable_compression = false, \
        .enable_encryption = false, \
        .thread_safe = false, \
    }, \
    .auth_required = false, \
    .auth_key_id = 0, \
    .auth_provider = NULL, \
    .route_table = NULL, \
    .route_table_len = 0, \
    .rx_callback = NULL, \
    .error_callback = NULL, \
    .callback_user_data = NULL, \
}

/**
 * \brief           Production configuration preset
 * \details         Production profile requires authentication by default.
 *                  The application must provide auth_provider before validate/create.
 */
#define XGL_CONFIG_PRESET_PRODUCTION { \
    .name = "production", \
    .source_id = 1, \
    .memory = { \
        .tx_pool_size = 8192, \
        .rx_buffer_size = 1056, \
        .allocator = NULL, \
    }, \
    .protocol = { \
        .ack_timeout_ms = 1000, \
        .max_retry_count = 7, \
        .window_size = 16, \
        .max_frame_size = 1024, \
    }, \
    .features = { \
        .enable_fragmentation = true, \
        .enable_compression = false, \
        .enable_encryption = false, \
        .thread_safe = false, \
    }, \
    .auth_required = true, \
    .auth_key_id = 1, \
    .auth_provider = NULL, \
    .route_table = NULL, \
    .route_table_len = 0, \
    .rx_callback = NULL, \
    .error_callback = NULL, \
    .callback_user_data = NULL, \
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_TYPES_H */
