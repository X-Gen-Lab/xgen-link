/**
 * \file            xgl_instance.c
 * \brief           Protocol instance management implementation
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>
#include <xgl/internal/xgl_allocator.h>
#include <xgl/internal/xgl_tiered_pool.h>
#include <xgl/internal/xgl_packet_pool.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_window.h>
#include <xgl/internal/xgl_rtt.h>
#include <xgl/internal/xgl_parser.h>
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
    allocator = config->memory.allocator;
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
    if (handle->config.features.thread_safe) {
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
    small_count = handle->config.memory.tx_pool_size / XGL_TIERED_POOL_SMALL_SIZE * 4 / 10;
    medium_count = handle->config.memory.tx_pool_size / XGL_TIERED_POOL_MEDIUM_SIZE * 4 / 10;
    large_count = handle->config.memory.tx_pool_size / XGL_TIERED_POOL_LARGE_SIZE * 2 / 10;

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
    packet_count = handle->config.memory.tx_pool_size / 256;
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

    if (handle->config.route_table_len > 0) {
        handle->route_last_read_count = handle->config.route_table_len;
        handle->route_last_read_ms = (uint32_t*)xgl_alloc(
            handle->allocator,
            handle->route_last_read_count * sizeof(uint32_t)
        );
        if (handle->route_last_read_ms == NULL) {
            err = XGL_ERR_NO_MEMORY;
            goto cleanup_route_table;
        }
        memset(handle->route_last_read_ms,
               0,
               handle->route_last_read_count * sizeof(uint32_t));
    }

    /* Allocate RX buffer for datalink layer */
    size_t rx_buffer_size = handle->config.memory.rx_buffer_size;
    uint8_t* rx_buffer = (uint8_t*)xgl_alloc(handle->allocator, rx_buffer_size);
    if (rx_buffer == NULL) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup_route_read_times;
    }

    /* Initialize data link layer */
    xgl_datalink_config_t datalink_config = {
        .rx_cache = rx_buffer,
        .rx_cache_size = rx_buffer_size,
        .source_id = handle->config.source_id,
        .stats = &handle->stats.datalink,
        .rx_header_crc_errors = &handle->stats.rx_header_crc_errors,
        .rx_crc16_errors = &handle->stats.rx_crc16_errors,
        .upper_layer = NULL,  /* Will be set after network layer init */
        .error_callback = handle->config.error_callback,
        .callback_user_data = handle->config.callback_user_data,
        .owner_handle = handle,
        .allocator = handle->allocator,
        .auth_required = handle->config.auth_required,
        .auth_key_id = handle->config.auth_key_id,
        .auth_provider = handle->config.auth_provider
    };
    err = xgl_datalink_init(&handle->layers.datalink_ctx, &datalink_config);
    if (err != XGL_OK) {
        goto cleanup_rx_buffer;
    }

    /* Initialize network layer */
    xgl_network_config_t network_config = {
        .local_id = handle->config.source_id,
        .route_table = &handle->route_table,
        .upper_layer = NULL,  /* Will be set after transport layer init */
        .lower_layer = NULL,  /* Will be set after creating datalink interface */
        .error_callback = handle->config.error_callback,
        .callback_user_data = handle->config.callback_user_data,
        .stats = &handle->stats.network,
        .auth_required = handle->config.auth_required,
        .auth_key_id = handle->config.auth_key_id,
        .auth_provider = handle->config.auth_provider
    };
    err = xgl_network_init(&handle->layers.network_ctx, &network_config);
    if (err != XGL_OK) {
        goto cleanup_rx_buffer;
    }

    /* Initialize transport layer */
    xgl_transport_config_t transport_config = {
        .local_id = handle->config.source_id,
        .max_retry_count = handle->config.protocol.max_retry_count,
        .default_timeout_ms = handle->config.protocol.ack_timeout_ms,
        .window_size = handle->config.protocol.window_size,
        .enable_fragmentation = handle->config.features.enable_fragmentation,
        .max_frame_size = handle->config.protocol.max_frame_size,
        .auth_tag_len = (handle->config.auth_required &&
                         handle->config.auth_provider != NULL) ?
                        (uint8_t)handle->config.auth_provider->tag_len : 0U,
        .route_table = &handle->route_table,
        .lower_layer = NULL,  /* Will be set after creating network interface */
        .rx_callback = handle->config.rx_callback,
        .error_callback = handle->config.error_callback,
        .callback_user_data = handle->config.callback_user_data,
        .stats = &handle->stats.transport,
        .tx_retries = &handle->stats.tx_retries,
        .allocator = handle->allocator
    };
    err = xgl_transport_init(&handle->layers.transport_ctx, &transport_config);
    if (err != XGL_OK) {
        goto cleanup_rx_buffer;
    }

    /* Create layer interfaces */
    err = xgl_datalink_get_interface(&handle->layers.datalink_ctx, &handle->layers.datalink_iface);
    if (err != XGL_OK) {
        goto cleanup_rx_buffer;
    }

    err = xgl_network_get_interface(&handle->layers.network_ctx, &handle->layers.network_iface);
    if (err != XGL_OK) {
        goto cleanup_rx_buffer;
    }

    err = xgl_transport_get_interface(&handle->layers.transport_ctx, &handle->layers.transport_iface);
    if (err != XGL_OK) {
        goto cleanup_rx_buffer;
    }

    /* Wire up layer interfaces */
    /* Datalink -> Network -> Transport -> Application */
    handle->layers.datalink_ctx.upper_layer = &handle->layers.network_iface;
    handle->layers.network_ctx.lower_layer = &handle->layers.datalink_iface;
    handle->layers.network_ctx.upper_layer = &handle->layers.transport_iface;
    handle->layers.transport_ctx.lower_layer = &handle->layers.network_iface;

    /* Mark as initialized */
    handle->initialized = true;

    return XGL_OK;

    /* Cleanup on error */
cleanup_rx_buffer:
    xgl_free(handle->allocator, rx_buffer);

cleanup_route_read_times:
    xgl_free(handle->allocator, handle->route_last_read_ms);
    handle->route_last_read_ms = NULL;
    handle->route_last_read_count = 0;

cleanup_route_table:
    xgl_route_table_destroy(&handle->route_table);

cleanup_packet_pool:
    xgl_packet_pool_destroy(&handle->packet_pool);

cleanup_tx_pool:
    xgl_tiered_pool_destroy(&handle->tx_pool);

cleanup:
#ifdef XGL_THREAD_SAFE
    if (handle->config.features.thread_safe) {
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
    /* Validate handle */
    if (handle == NULL) {
        return;
    }

    /* Destroy transport layer */
    xgl_transport_destroy(&handle->layers.transport_ctx);

    /* Free datalink layer's RX buffer */
    if (handle->layers.datalink_ctx.rx_cache != NULL) {
        xgl_free(handle->allocator, handle->layers.datalink_ctx.rx_cache);
        handle->layers.datalink_ctx.rx_cache = NULL;
    }

    /* Network and datalink layers don't need explicit destroy */

    if (handle->route_last_read_ms != NULL) {
        xgl_free(handle->allocator, handle->route_last_read_ms);
        handle->route_last_read_ms = NULL;
        handle->route_last_read_count = 0;
    }

    /* Destroy route table */
    xgl_route_table_destroy(&handle->route_table);

    /* Destroy packet pool */
    xgl_packet_pool_destroy(&handle->packet_pool);

    /* Destroy tiered pool */
    xgl_tiered_pool_destroy(&handle->tx_pool);

#ifdef XGL_THREAD_SAFE
    /* Destroy mutex if thread safety was enabled */
    if (handle->config.features.thread_safe) {
        xgl_mutex_destroy(&handle->mutex);
    }
#endif

    /* Free instance structure itself */
    xgl_free(handle->allocator, handle);
}
