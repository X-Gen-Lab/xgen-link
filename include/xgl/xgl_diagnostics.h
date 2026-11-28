/**
 * \file            xgl_diagnostics.h
 * \brief           Resource estimates and runtime observations
 * \author          X-Gen Lab
 */

#ifndef XGL_DIAGNOSTICS_H
#define XGL_DIAGNOSTICS_H

#include <xgl/xgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Exact reserved bytes by ownership and resource category
 * \note            Category bytes include alignment and sum to
 * requirements.size.
 */
typedef struct {
    xgl_memory_requirements_t requirements; /**< Complete storage contract */
    size_t management_bytes;     /**< Instance and allocator descriptors */
    size_t initialization_bytes; /**< Routes, links and per-PHY RX caches */
    size_t peer_bytes;           /**< Reliable scope records */
    size_t tx_record_bytes;      /**< Outstanding reliable packet records */
    size_t tx_payload_bytes;     /**< Reserved reliable payload copies */
    size_t scratch_bytes;        /**< Synchronous frame scratch storage */
    size_t rx_buffered_bytes;    /**< Out-of-order records, payload and TLVs */
} xgl_memory_report_t;

/** \brief           Current owned resources; this is not reserved workspace
 * size. */
typedef struct {
    size_t peer_count;          /**< Existing scopes */
    size_t failed_peers;        /**< Terminal scopes requiring explicit close */
    size_t tx_packets;          /**< Retained reliable records */
    size_t tx_payload_bytes;    /**< Retained packet payload bytes */
    size_t rx_buffered_packets; /**< Out-of-order retained packets */
    size_t rx_buffered_bytes;   /**< Out-of-order payload bytes */
} xgl_resource_snapshot_t;

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
    uint32_t
        max_rtt_ms; /**< Largest accepted first-transmission ACK sample in ms */
    uint32_t min_rtt_ms; /**< Smallest RTT sample in ms; UINT32_MAX before a
                            sample */

    /*-----------------------------------------------------------------------*/
    /* Memory Usage                                                          */
    /*-----------------------------------------------------------------------*/
    size_t memory_used; /**< Total workspace bytes reserved for the instance */
    size_t memory_peak; /**< Peak reservation; equals memory_used for fixed
                           workspace */
} xgl_statistics_t;

/**
 * \brief           Explain reserved storage without allocating or initializing
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[out]      report: Byte breakdown; unchanged on failure
 * \return          XGL_OK or a configuration or capacity error
 */
xgl_error_t xgl_memory_report(const xgl_config_t* config,
                              xgl_memory_report_t* report);

/**
 * \brief           Inspect currently owned resources without allocation
 * \param[in]       handle: Caller-serialized initialized instance
 * \param[out]      snapshot: Current resource counts, unchanged on error
 * \return          XGL_OK, argument error, or UNSUPPORTED if disabled
 */
xgl_error_t xgl_resources_get(xgl_handle_t handle,
                              xgl_resource_snapshot_t* snapshot);

/**
 * \brief           Copy counters under caller-serialized instance access
 * \param[in]       handle: Initialized instance
 * \param[out]      stats: Snapshot destination; unchanged when unsupported
 * \return          XGL_OK, XGL_ERR_NULL_POINTER, XGL_ERR_NOT_INITIALIZED, or
 *                  XGL_ERR_UNSUPPORTED when statistics are compiled out
 */
xgl_error_t xgl_stats_get(xgl_handle_t handle, xgl_statistics_t* stats);

/**
 * \brief           Reset observations while preserving the workspace
 * reservation
 * \param[in]       handle: Initialized instance
 * \return          XGL_OK, XGL_ERR_NULL_POINTER, XGL_ERR_NOT_INITIALIZED, or
 *                  XGL_ERR_UNSUPPORTED when statistics are compiled out
 */
xgl_error_t xgl_stats_reset(xgl_handle_t handle);

/**
 * \brief           Read the library version as an integer
 * \return          MAJOR * 10000 + MINOR * 100 + PATCH
 */
uint32_t xgl_version_int(void);

/**
 * \brief           Get error string description
 * \param[in]       error: Error code
 * \return          Error description string (never NULL)
 */
const char* xgl_error_string(xgl_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* XGL_DIAGNOSTICS_H */
