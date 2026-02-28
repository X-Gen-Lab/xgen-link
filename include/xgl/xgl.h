/**
 * \file            xgl.h
 * \brief           x_gen_link Protocol Stack - Main Public API
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-02-28
 *
 * \copyright       Copyright (c) 2026 Nexus Team
 *
 * \details         x_gen_link is a modern, robust, and highly configurable
 *                  communication protocol stack designed for resource-constrained
 *                  embedded systems. It provides reliable data transmission with
 *                  support for multiple instances, thread safety, adaptive
 *                  retransmission, and comprehensive error handling.
 *
 * \par Features
 *                  - Multi-instance architecture for multiple communication channels
 *                  - Zero-copy transmission for minimal memory overhead
 *                  - Adaptive retransmission with RTT estimation (RFC 6298)
 *                  - Sliding window flow control
 *                  - Packet fragmentation and reassembly
 *                  - Optional thread safety for RTOS environments
 *                  - Configurable memory pools for deterministic allocation
 *                  - Comprehensive error handling and statistics
 *                  - Platform abstraction for portability
 *                  - Minimal footprint: 32KB RAM, 50KB Flash (tiny config)
 *
 * \par Quick Start Example
 * \code{.c}
 * #include "xgl/xgl.h"
 *
 * // Physical layer callbacks
 * xgl_error_t uart_tx(const uint8_t* data, size_t len, void* user_data) {
 *     // Send data via UART
 *     return XGL_OK;
 * }
 *
 * xgl_error_t uart_rx(uint8_t* buffer, size_t* len, void* user_data) {
 *     // Receive data from UART
 *     return XGL_OK;
 * }
 *
 * // Receive callback
 * void on_receive(xgl_handle_t handle, uint8_t source_id, uint8_t data_type,
 *                 const uint8_t* data, size_t len, void* user_data) {
 *     printf("Received %zu bytes from node %d\n", len, source_id);
 * }
 *
 * int main(void) {
 *     // Setup physical layer
 *     xgl_phy_ops_t phy = {
 *         .tx = uart_tx,
 *         .rx = uart_rx,
 *         .user_data = NULL
 *     };
 *
 *     // Setup route table
 *     xgl_route_item_t routes[] = {
 *         { .target_id = 2, .phy = &phy, .max_frame_size = 256, .read_freq_hz = 100 }
 *     };
 *
 *     // Get default configuration
 *     xgl_config_t config;
 *     xgl_config_get_default(&config);
 *     config.source_id = 1;
 *     config.route_table = routes;
 *     config.route_table_len = 1;
 *     config.rx_callback = on_receive;
 *
 *     // Create and initialize protocol instance
 *     xgl_handle_t handle = xgl_create(&config);
 *     if (handle == NULL) {
 *         return -1;
 *     }
 *
 *     if (xgl_init(handle) != XGL_OK) {
 *         xgl_destroy(handle);
 *         return -1;
 *     }
 *
 *     // Send data
 *     xgl_tx_data_t tx_data = {
 *         .target_id = 2,
 *         .data_type = 0x01,
 *         .data = (const uint8_t*)"Hello",
 *         .data_len = 5,
 *         .reliable = true,
 *         .priority = 0
 *     };
 *     xgl_send(handle, &tx_data);
 *
 *     // Main loop
 *     while (1) {
 *         xgl_run(handle, 100);  // Call at 100 Hz
 *         delay_ms(10);
 *     }
 *
 *     // Cleanup
 *     xgl_destroy(handle);
 *     return 0;
 * }
 * \endcode
 *
 * \par Zero-Copy Example
 * \code{.c}
 * // Allocate buffer with header space
 * uint8_t buffer[XGL_FRAME_HEADER_SIZE + 100];
 *
 * // Write data after header space
 * memcpy(buffer + XGL_FRAME_HEADER_SIZE, "Hello", 5);
 *
 * // Send without copying
 * xgl_tx_data_zerocopy_t tx_data = {
 *     .buffer = buffer,
 *     .buffer_size = sizeof(buffer),
 *     .data_offset = XGL_FRAME_HEADER_SIZE,
 *     .data_len = 5,
 *     .target_id = 2,
 *     .data_type = 0x01,
 *     .reliable = true,
 *     .priority = 0
 * };
 * xgl_send_zerocopy(handle, &tx_data);
 * \endcode
 *
 * \par Multi-Instance Example
 * \code{.c}
 * // Create multiple independent instances
 * xgl_handle_t uart_handle = xgl_create(&uart_config);
 * xgl_handle_t spi_handle = xgl_create(&spi_config);
 *
 * xgl_init(uart_handle);
 * xgl_init(spi_handle);
 *
 * // Each instance operates independently
 * xgl_send(uart_handle, &uart_tx_data);
 * xgl_send(spi_handle, &spi_tx_data);
 *
 * // Process each instance
 * xgl_run(uart_handle, 100);
 * xgl_run(spi_handle, 100);
 * \endcode
 */

#ifndef XGL_H
#define XGL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------*/
/* Public Header Includes                                                    */
/*---------------------------------------------------------------------------*/

/* Core types and error handling */
#include "xgl_error.h"
#include "xgl_types.h"

/* Data link layer */
#include "xgl_crc.h"
#include "xgl_serialize.h"
#include "xgl_frame.h"
#include "xgl_parser.h"
#include "xgl_datalink.h"

/* Network layer */
#include "xgl_route.h"
#include "xgl_network.h"
#include "xgl_sequence.h"

/* Transport layer */
#include "xgl_rtt.h"
#include "xgl_window.h"
#include "xgl_reliable.h"
#include "xgl_ack.h"
#include "xgl_fragment.h"
#include "xgl_transport.h"

/* Memory management */
#include "xgl_allocator.h"
#include "xgl_mempool.h"
#include "xgl_tiered_pool.h"
#include "xgl_packet_pool.h"

/* Utilities */
#include "xgl_list.h"
#include "xgl_hashtable.h"

/* Platform abstraction */
#include "xgl_platform.h"
#include "xgl_mutex.h"
#include "xgl_time.h"
#include "xgl_atomic.h"

/*---------------------------------------------------------------------------*/
/* Version Information                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Protocol major version
 * \note            Incremented for breaking API changes
 */
#define XGL_VERSION_MAJOR       1

/**
 * \brief           Protocol minor version
 * \note            Incremented for new features (backward compatible)
 */
#define XGL_VERSION_MINOR       0

/**
 * \brief           Protocol patch version
 * \note            Incremented for bug fixes
 */
#define XGL_VERSION_PATCH       0

/**
 * \brief           Protocol version string
 */
#define XGL_VERSION_STRING      "1.0.0"

/**
 * \brief           Protocol version as integer (MAJOR * 10000 + MINOR * 100 + PATCH)
 * \note            Useful for compile-time version checks
 */
#define XGL_VERSION_INT         ((XGL_VERSION_MAJOR * 10000) + \
                                 (XGL_VERSION_MINOR * 100) + \
                                 XGL_VERSION_PATCH)

/**
 * \brief           Check if protocol version is at least the specified version
 * \param[in]       major: Major version
 * \param[in]       minor: Minor version
 * \param[in]       patch: Patch version
 * \return          1 if current version >= specified version, 0 otherwise
 */
#define XGL_VERSION_CHECK(major, minor, patch) \
    (XGL_VERSION_INT >= ((major) * 10000 + (minor) * 100 + (patch)))

/**
 * \brief           Get protocol version string at runtime
 * \return          Version string (e.g., "1.0.0")
 */
const char* xgl_version_string(void);

/**
 * \brief           Get protocol version as integer at runtime
 * \return          Version integer (MAJOR * 10000 + MINOR * 100 + PATCH)
 */
uint32_t xgl_version_int(void);

/*---------------------------------------------------------------------------*/
/* Instance Management API                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Create a new protocol instance
 * \param[in]       config: Configuration structure
 * \return          Instance handle on success, NULL on failure
 * \note            The configuration is copied internally
 * \note            Use xgl_init() to initialize the instance after creation
 * \note            Memory is allocated using the provided allocator or malloc
 * \warning         Must call xgl_destroy() to free resources
 *
 * \par Example
 * \code{.c}
 * xgl_config_t config;
 * xgl_config_get_default(&config);
 * config.source_id = 1;
 *
 * xgl_handle_t handle = xgl_create(&config);
 * if (handle == NULL) {
 *     printf("Failed to create instance\n");
 *     return -1;
 * }
 * \endcode
 */
xgl_handle_t xgl_create(const xgl_config_t* config);

/**
 * \brief           Initialize protocol instance
 * \param[in]       handle: Instance handle
 * \return          XGL_OK on success, error code otherwise
 * \note            Must be called after xgl_create() and before using instance
 * \note            Allocates all internal resources and initializes layers
 * \note            If initialization fails, partial resources are cleaned up
 * \warning         Do not use instance if initialization fails
 *
 * \par Example
 * \code{.c}
 * xgl_error_t err = xgl_init(handle);
 * if (err != XGL_OK) {
 *     printf("Initialization failed: %s\n", xgl_error_string(err));
 *     xgl_destroy(handle);
 *     return -1;
 * }
 * \endcode
 */
xgl_error_t xgl_init(xgl_handle_t handle);

/**
 * \brief           Destroy protocol instance and free all resources
 * \param[in]       handle: Instance handle
 * \note            Frees all allocated memory and invalidates the handle
 * \note            Safe to call with NULL handle
 * \note            Automatically cleans up pending packets and timers
 * \warning         Do not use handle after calling this function
 *
 * \par Example
 * \code{.c}
 * xgl_destroy(handle);
 * handle = NULL;  // Good practice
 * \endcode
 */
void xgl_destroy(xgl_handle_t handle);

/*---------------------------------------------------------------------------*/
/* Configuration API                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get default configuration
 * \param[out]      config: Configuration structure to fill
 * \note            Fills configuration with sensible defaults
 * \note            User should modify as needed before calling xgl_create()
 * \note            Default values: 4KB TX pool, 512B RX buffer, 5 retries
 *
 * \par Example
 * \code{.c}
 * xgl_config_t config;
 * xgl_config_get_default(&config);
 *
 * // Customize as needed
 * config.source_id = 1;
 * config.max_retry_count = 3;
 * config.thread_safe = true;
 * \endcode
 */
void xgl_config_get_default(xgl_config_t* config);

/**
 * \brief           Get tiny configuration preset
 * \param[out]      config: Configuration structure to fill
 * \note            Optimized for 32KB RAM, 50KB Flash
 * \note            Minimal features, suitable for very constrained MCUs
 * \note            Values: 1KB TX pool, 160B RX buffer, 128B max frame
 *
 * \par Example
 * \code{.c}
 * xgl_config_t config;
 * xgl_config_get_preset_tiny(&config);
 * config.source_id = 1;  // Must set source ID
 * \endcode
 */
void xgl_config_get_preset_tiny(xgl_config_t* config);

/**
 * \brief           Get small configuration preset
 * \param[out]      config: Configuration structure to fill
 * \note            Optimized for 64KB RAM, 100KB Flash
 * \note            Includes fragmentation support
 * \note            Values: 2KB TX pool, 288B RX buffer, 256B max frame
 */
void xgl_config_get_preset_small(xgl_config_t* config);

/**
 * \brief           Get medium configuration preset
 * \param[out]      config: Configuration structure to fill
 * \note            Optimized for 128KB RAM, 256KB Flash
 * \note            Includes fragmentation and compression
 * \note            Values: 4KB TX pool, 544B RX buffer, 512B max frame
 */
void xgl_config_get_preset_medium(xgl_config_t* config);

/**
 * \brief           Get large configuration preset
 * \param[out]      config: Configuration structure to fill
 * \note            Optimized for 256KB+ RAM, 512KB+ Flash
 * \note            Full features including encryption
 * \note            Values: 8KB TX pool, 1056B RX buffer, 1024B max frame
 */
void xgl_config_get_preset_large(xgl_config_t* config);

/**
 * \brief           Validate configuration parameters
 * \param[in]       config: Configuration structure to validate
 * \return          XGL_OK if valid, error code otherwise
 * \note            Checks all parameters for validity
 * \note            Should be called before xgl_create()
 * \note            Validates: pool sizes, retry counts, window size, etc.
 *
 * \par Example
 * \code{.c}
 * xgl_config_t config;
 * xgl_config_get_default(&config);
 * config.source_id = 1;
 *
 * xgl_error_t err = xgl_config_validate(&config);
 * if (err != XGL_OK) {
 *     printf("Invalid configuration: %s\n", xgl_error_string(err));
 *     return -1;
 * }
 * \endcode
 */
xgl_error_t xgl_config_validate(const xgl_config_t* config);

/*---------------------------------------------------------------------------*/
/* Send API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send data (standard mode with copy)
 * \param[in]       handle: Instance handle
 * \param[in]       tx_data: Transmission data structure
 * \return          XGL_OK on success, error code otherwise
 * \note            Data is copied internally
 * \note            Supports reliable and unreliable transmission
 * \note            Automatic fragmentation if data exceeds max frame size
 * \warning         Blocks if sliding window is full (returns XGL_ERR_WINDOW_FULL)
 *
 * \par Example - Reliable Send
 * \code{.c}
 * const char* message = "Hello, World!";
 * xgl_tx_data_t tx_data = {
 *     .target_id = 2,
 *     .data_type = 0x01,
 *     .data = (const uint8_t*)message,
 *     .data_len = strlen(message),
 *     .reliable = true,
 *     .priority = 0
 * };
 *
 * xgl_error_t err = xgl_send(handle, &tx_data);
 * if (err != XGL_OK) {
 *     printf("Send failed: %s\n", xgl_error_string(err));
 * }
 * \endcode
 *
 * \par Example - Unreliable Send
 * \code{.c}
 * uint8_t sensor_data[4] = {0x12, 0x34, 0x56, 0x78};
 * xgl_tx_data_t tx_data = {
 *     .target_id = 2,
 *     .data_type = 0x02,
 *     .data = sensor_data,
 *     .data_len = sizeof(sensor_data),
 *     .reliable = false,  // No ACK required
 *     .priority = 5       // Higher priority
 * };
 * xgl_send(handle, &tx_data);
 * \endcode
 */
xgl_error_t xgl_send(xgl_handle_t handle, const xgl_tx_data_t* tx_data);

/**
 * \brief           Send data (zero-copy mode)
 * \param[in]       handle: Instance handle
 * \param[in]       tx_data: Zero-copy transmission data structure
 * \return          XGL_OK on success, error code otherwise
 * \note            Buffer must have XGL_FRAME_HEADER_SIZE bytes reserved at start
 * \note            No data copy is performed (50% reduction in memory bandwidth)
 * \note            Buffer ownership transfers to protocol stack temporarily
 * \warning         Do not modify buffer until transmission completes
 *
 * \par Example
 * \code{.c}
 * // Allocate buffer with header space
 * uint8_t buffer[XGL_FRAME_HEADER_SIZE + 100];
 *
 * // Write data after header space
 * uint8_t* data_ptr = buffer + XGL_FRAME_HEADER_SIZE;
 * memcpy(data_ptr, "Zero-copy data", 14);
 *
 * // Send without copying
 * xgl_tx_data_zerocopy_t tx_data = {
 *     .buffer = buffer,
 *     .buffer_size = sizeof(buffer),
 *     .data_offset = XGL_FRAME_HEADER_SIZE,
 *     .data_len = 14,
 *     .target_id = 2,
 *     .data_type = 0x01,
 *     .reliable = true,
 *     .priority = 0
 * };
 *
 * xgl_error_t err = xgl_send_zerocopy(handle, &tx_data);
 * if (err != XGL_OK) {
 *     printf("Zero-copy send failed: %s\n", xgl_error_string(err));
 * }
 * \endcode
 */
xgl_error_t xgl_send_zerocopy(xgl_handle_t handle, 
                              const xgl_tx_data_zerocopy_t* tx_data);

/*---------------------------------------------------------------------------*/
/* Statistics API                                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get protocol statistics
 * \param[in]       handle: Instance handle
 * \param[out]      stats: Statistics structure to fill
 * \return          XGL_OK on success, error code otherwise
 * \note            Statistics are updated atomically
 * \note            Includes TX/RX counters, errors, RTT metrics, memory usage
 *
 * \par Example
 * \code{.c}
 * xgl_statistics_t stats;
 * xgl_error_t err = xgl_stats_get(handle, &stats);
 * if (err == XGL_OK) {
 *     printf("TX packets: %llu\n", stats.tx_packets);
 *     printf("RX packets: %llu\n", stats.rx_packets);
 *     printf("TX errors: %llu\n", stats.tx_errors);
 *     printf("RX errors: %llu\n", stats.rx_errors);
 *     printf("Avg RTT: %u ms\n", stats.avg_rtt_ms);
 *     printf("Memory used: %zu bytes\n", stats.memory_used);
 * }
 * \endcode
 */
xgl_error_t xgl_stats_get(xgl_handle_t handle, xgl_statistics_t* stats);

/**
 * \brief           Reset protocol statistics
 * \param[in]       handle: Instance handle
 * \return          XGL_OK on success, error code otherwise
 * \note            Resets all counters to zero atomically
 * \note            Does not affect protocol operation
 *
 * \par Example
 * \code{.c}
 * // Reset statistics at start of test
 * xgl_stats_reset(handle);
 *
 * // Run test...
 *
 * // Get statistics after test
 * xgl_statistics_t stats;
 * xgl_stats_get(handle, &stats);
 * \endcode
 */
xgl_error_t xgl_stats_reset(xgl_handle_t handle);

/*---------------------------------------------------------------------------*/
/* Runtime Processing                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Run protocol processing (call periodically)
 * \param[in]       handle: Instance handle
 * \param[in]       freq_hz: Calling frequency in Hz
 * \note            Handles timeouts, retransmissions, and RX processing
 * \note            Should be called from main loop or timer interrupt
 * \note            Typical frequencies: 10-1000 Hz depending on requirements
 * \note            Higher frequency = lower latency, higher CPU usage
 * \warning         Must be called regularly for protocol to function
 *
 * \par Example - Main Loop (Bare Metal)
 * \code{.c}
 * while (1) {
 *     xgl_run(handle, 100);  // Call at 100 Hz
 *     delay_ms(10);          // 10ms delay = 100 Hz
 * }
 * \endcode
 *
 * \par Example - Timer Interrupt (RTOS)
 * \code{.c}
 * void timer_callback(void* arg) {
 *     xgl_handle_t handle = (xgl_handle_t)arg;
 *     xgl_run(handle, 1000);  // Called at 1000 Hz
 * }
 *
 * // Setup 1ms timer
 * timer_start(1, timer_callback, handle);
 * \endcode
 *
 * \par Example - Multiple Instances
 * \code{.c}
 * while (1) {
 *     xgl_run(uart_handle, 100);
 *     xgl_run(spi_handle, 100);
 *     delay_ms(10);
 * }
 * \endcode
 */
void xgl_run(xgl_handle_t handle, uint32_t freq_hz);

/*---------------------------------------------------------------------------*/
/* Best Practices and Usage Notes                                            */
/*---------------------------------------------------------------------------*/

/**
 * \par Thread Safety
 *      When thread_safe is enabled in configuration:
 *      - All API functions are thread-safe
 *      - Multiple threads can call xgl_send() concurrently
 *      - xgl_run() can be called from a different thread than xgl_send()
 *      - Callbacks are invoked with mutex held (keep them short)
 *
 * \par Memory Management
 *      - Use custom allocator for deterministic allocation
 *      - Memory pools eliminate heap fragmentation
 *      - Zero-copy API reduces memory bandwidth by 50%
 *      - All memory is freed on xgl_destroy()
 *
 * \par Error Handling
 *      - Always check return values from API functions
 *      - Register error callback for asynchronous errors
 *      - Use xgl_error_string() to get human-readable error messages
 *      - Check statistics for error counters
 *
 * \par Performance Optimization
 *      - Use zero-copy API for high-throughput applications
 *      - Adjust window size based on RTT and bandwidth
 *      - Use unreliable transmission for time-sensitive data
 *      - Call xgl_run() at appropriate frequency (higher = lower latency)
 *
 * \par Porting to New Platform
 *      1. Implement physical layer callbacks (tx/rx)
 *      2. Implement platform abstraction (xgl_time_ms, xgl_delay_ms)
 *      3. Implement mutex functions if thread safety needed
 *      4. Test with property-based tests
 *
 * \par Common Pitfalls
 *      - Forgetting to call xgl_run() periodically
 *      - Not reserving header space for zero-copy buffers
 *      - Blocking in callbacks (keep them short)
 *      - Not checking return values
 *      - Using handle after xgl_destroy()
 *
 * \par Resource Requirements
 *      Tiny:   32KB RAM,  50KB Flash  (basic features)
 *      Small:  64KB RAM, 100KB Flash  (+ fragmentation)
 *      Medium: 128KB RAM, 256KB Flash (+ compression)
 *      Large:  256KB RAM, 512KB Flash (+ encryption)
 *
 * \par Typical Use Cases
 *      - Sensor networks (unreliable, low power)
 *      - Industrial control (reliable, real-time)
 *      - Firmware updates (reliable, fragmentation)
 *      - Multi-node networks (routing, forwarding)
 *      - UART/SPI/I2C/CAN communication
 *
 * \par Further Reading
 *      - User Guide: docs/user_guide.md
 *      - Architecture: docs/architecture.md
 *      - Porting Guide: docs/porting.md
 *      - Examples: examples/
 *      - API Reference: https://nexus-team.github.io/x_gen_link/
 */

#ifdef __cplusplus
}
#endif

#endif /* XGL_H */
