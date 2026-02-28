/**
 * \file            xgl_instance.c
 * \brief           Protocol instance management implementation
 * \author          Nexus Team
 */

#include <xgl/xgl.h>
#include <xgl/xgl_tiered_pool.h>
#include <xgl/xgl_packet_pool.h>
#include <xgl/xgl_route.h>
#include <xgl/xgl_window.h>
#include <xgl/xgl_rtt.h>
#include <xgl/xgl_parser.h>
#include "xgl_instance_internal.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Instance Creation                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Create a new protocol instance
 * \details         Allocates memory for instance structure and stores configuration
 */
xgl_handle_t xgl_create(const xgl_config_t* config) {
    xgl_handle_t handle;
    xgl_allocator_t* allocator;
    xgl_error_t err;
    
    /* Validate configuration using public validation function */
    err = xgl_config_validate(config);
    if (err != XGL_OK) {
        return NULL;
    }
    
    /* Determine allocator to use */
    allocator = config->allocator;
    if (allocator == NULL) {
        allocator = xgl_allocator_get_default();
    }
    
    /* Allocate instance structure */
    handle = (xgl_handle_t)xgl_alloc(allocator, sizeof(struct xgl_instance));
    if (handle == NULL) {
        return NULL;
    }
    
    /* Zero-initialize the structure */
    memset(handle, 0, sizeof(struct xgl_instance));
    
    /* Store configuration */
    memcpy(&handle->config, config, sizeof(xgl_config_t));
    handle->allocator = allocator;
    handle->initialized = false;
    
    return handle;
}

/*---------------------------------------------------------------------------*/
/* Instance Initialization                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize protocol instance
 * \details         Initializes all layers and allocates resources
 */
xgl_error_t xgl_init(xgl_handle_t handle) {
    xgl_error_t err;
    size_t i;
    size_t small_count, medium_count, large_count;
    size_t packet_count;
    
    /* Validate handle */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check if already initialized */
    if (handle->initialized) {
        return XGL_ERR_ALREADY_INITIALIZED;
    }
    
#ifdef XGL_THREAD_SAFE
    /* Initialize mutex if thread safety is enabled */
    if (handle->config.thread_safe) {
        err = xgl_mutex_init(&handle->mutex);
        if (err != XGL_OK) {
            goto cleanup;
        }
    }
#endif
    
    /* Initialize statistics */
    memset(&handle->stats, 0, sizeof(xgl_statistics_t));
    handle->stats.min_rtt_ms = UINT32_MAX;
    
    /* Calculate pool sizes based on configuration */
    /* Allocate ~40% small, ~40% medium, ~20% large blocks */
    small_count = handle->config.tx_pool_size / XGL_TIERED_POOL_SMALL_SIZE * 4 / 10;
    medium_count = handle->config.tx_pool_size / XGL_TIERED_POOL_MEDIUM_SIZE * 4 / 10;
    large_count = handle->config.tx_pool_size / XGL_TIERED_POOL_LARGE_SIZE * 2 / 10;
    
    /* Ensure at least one block of each size */
    if (small_count == 0) small_count = 1;
    if (medium_count == 0) medium_count = 1;
    if (large_count == 0) large_count = 1;
    
    /* Initialize tiered memory pool */
    if (xgl_tiered_pool_init(&handle->tx_pool, small_count, medium_count, 
                             large_count) != 0) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup;
    }
    
    /* Calculate packet pool size (estimate ~10% of TX pool size) */
    packet_count = handle->config.tx_pool_size / 256;
    if (packet_count < 4) packet_count = 4;  /* Minimum 4 packets */
    if (packet_count > 64) packet_count = 64; /* Maximum 64 packets */
    
    /* Initialize packet object pool */
    if (xgl_packet_pool_init(&handle->packet_pool, packet_count, 
                             handle->allocator) != 0) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup_tx_pool;
    }
    
    /* Initialize route table */
    err = xgl_route_table_init(&handle->route_table, 
                               XGL_ROUTE_TABLE_DEFAULT_SIZE,
                               handle->allocator);
    if (err != XGL_OK) {
        goto cleanup_packet_pool;
    }
    
    /* Load routes from configuration */
    if (handle->config.route_table_len > 0) {
        err = xgl_route_table_load(&handle->route_table,
                                   handle->config.route_table,
                                   handle->config.route_table_len);
        if (err != XGL_OK) {
            goto cleanup_route_table;
        }
    }
    
    /* Allocate sequence numbers array (one per possible target ID) */
    handle->seq_numbers_count = 256;  /* 8-bit ID space */
    handle->seq_numbers = (uint8_t*)xgl_alloc(handle->allocator, 
                                              handle->seq_numbers_count);
    if (handle->seq_numbers == NULL) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup_route_table;
    }
    memset(handle->seq_numbers, 0, handle->seq_numbers_count);
    
    /* Allocate sliding windows array (one per possible target ID) */
    handle->windows_count = 256;
    handle->windows = (xgl_sliding_window_t*)xgl_alloc(handle->allocator,
                                                       handle->windows_count * 
                                                       sizeof(xgl_sliding_window_t));
    if (handle->windows == NULL) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup_seq_numbers;
    }
    
    /* Initialize each sliding window */
    for (i = 0; i < handle->windows_count; i++) {
        handle->windows[i].window_size = handle->config.window_size;
        handle->windows[i].send_base = 0;
        handle->windows[i].next_seq_num = 0;
        handle->windows[i].expected_seq_num = 0;
        handle->windows[i].ack_received = NULL;  /* Allocated on demand */
    }
    
    /* Allocate RTT estimators array (one per possible target ID) */
    handle->rtt_est = (xgl_rtt_estimator_t*)xgl_alloc(handle->allocator,
                                                      handle->windows_count * 
                                                      sizeof(xgl_rtt_estimator_t));
    if (handle->rtt_est == NULL) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup_windows;
    }
    
    /* Initialize each RTT estimator */
    for (i = 0; i < handle->windows_count; i++) {
        handle->rtt_est[i].srtt = 0;
        handle->rtt_est[i].rttvar = 0;
        handle->rtt_est[i].rto = (int32_t)handle->config.ack_timeout_ms;
    }
    
    /* Initialize wait-ACK list */
    xgl_list_init(&handle->wait_ack_list);
    
    /* Initialize RX parser list */
    xgl_list_init(&handle->rx_parser_list);
    
    /* Allocate RX buffer */
    handle->rx_buffer_size = handle->config.rx_buffer_size;
    handle->rx_buffer = (uint8_t*)xgl_alloc(handle->allocator, 
                                            handle->rx_buffer_size);
    if (handle->rx_buffer == NULL) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup_rtt_est;
    }
    
    /* Mark as initialized */
    handle->initialized = true;
    
    return XGL_OK;
    
    /* Cleanup on error */
cleanup_rtt_est:
    xgl_free(handle->allocator, handle->rtt_est);
    handle->rtt_est = NULL;
    
cleanup_windows:
    xgl_free(handle->allocator, handle->windows);
    handle->windows = NULL;
    
cleanup_seq_numbers:
    xgl_free(handle->allocator, handle->seq_numbers);
    handle->seq_numbers = NULL;
    
cleanup_route_table:
    xgl_route_table_destroy(&handle->route_table);
    
cleanup_packet_pool:
    xgl_packet_pool_destroy(&handle->packet_pool);
    
cleanup_tx_pool:
    xgl_tiered_pool_destroy(&handle->tx_pool);
    
cleanup:
#ifdef XGL_THREAD_SAFE
    if (handle->config.thread_safe) {
        xgl_mutex_destroy(&handle->mutex);
    }
#endif
    
    return err;
}

/*---------------------------------------------------------------------------*/
/* Instance Destruction                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Destroy protocol instance and free all resources
 * \details         Cleans up all allocated resources and frees instance
 */
void xgl_destroy(xgl_handle_t handle) {
    size_t i;
    
    /* Validate handle */
    if (handle == NULL) {
        return;
    }
    
    /* Free RX buffer */
    if (handle->rx_buffer != NULL) {
        xgl_free(handle->allocator, handle->rx_buffer);
        handle->rx_buffer = NULL;
    }
    
    /* Free RTT estimators */
    if (handle->rtt_est != NULL) {
        xgl_free(handle->allocator, handle->rtt_est);
        handle->rtt_est = NULL;
    }
    
    /* Free sliding windows and their ACK bitmaps */
    if (handle->windows != NULL) {
        for (i = 0; i < handle->windows_count; i++) {
            if (handle->windows[i].ack_received != NULL) {
                xgl_free(handle->allocator, handle->windows[i].ack_received);
                handle->windows[i].ack_received = NULL;
            }
        }
        xgl_free(handle->allocator, handle->windows);
        handle->windows = NULL;
    }
    
    /* Free sequence numbers */
    if (handle->seq_numbers != NULL) {
        xgl_free(handle->allocator, handle->seq_numbers);
        handle->seq_numbers = NULL;
    }
    
    /* Destroy route table */
    xgl_route_table_destroy(&handle->route_table);
    
    /* Destroy packet pool */
    xgl_packet_pool_destroy(&handle->packet_pool);
    
    /* Destroy tiered pool */
    xgl_tiered_pool_destroy(&handle->tx_pool);
    
#ifdef XGL_THREAD_SAFE
    /* Destroy mutex if thread safety was enabled */
    if (handle->config.thread_safe) {
        xgl_mutex_destroy(&handle->mutex);
    }
#endif
    
    /* Free instance structure itself */
    xgl_free(handle->allocator, handle);
}

/*---------------------------------------------------------------------------*/
/* Version Information                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get protocol version string at runtime
 */
const char* xgl_version_string(void) {
    return XGL_VERSION_STRING;
}

/**
 * \brief           Get protocol version as integer at runtime
 */
uint32_t xgl_version_int(void) {
    return XGL_VERSION_INT;
}
