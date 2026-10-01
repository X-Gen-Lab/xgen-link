/**
 * \file            xgl_instance.c
 * \brief           Protocol instance management implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_parser.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_rtt.h>
#include <xgl/internal/xgl_window.h>
#include <xgl/xgl.h>

#include <string.h>
#include <xgen/memory/allocator.h>

#include "xgl_instance_internal.h"

/*---------------------------------------------------------------------------*/
/* Instance Initialization                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize protocol instance
 * \details         Initializes all layers and allocates resources
 */
xgl_error_t xgl_init(xgl_handle_t handle) {
    xgl_error_t err;

    /* Validate handle */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Check if already initialized */
    if (handle->initialized) {
        return XGL_ERR_ALREADY_INITIALIZED;
    }

    /* Initialize statistics */
    memset(&handle->stats, 0, sizeof(xgl_statistics_t));
    handle->stats.min_rtt_ms = UINT32_MAX;

    /* Initialize route table */
    err = xgl_route_table_init(&handle->route_table,
                               handle->config->route_table_len,
                               handle->allocator);
    if (err != XGL_OK) {
        goto cleanup;
    }

    /* Load routes from configuration */
    if (handle->config->route_table_len > 0) {
        err = xgl_route_table_load(&handle->route_table,
                                   handle->config->route_table,
                                   handle->config->route_table_len);
        if (err != XGL_OK) {
            goto cleanup_route_table;
        }
    }

    /* Allocate RX buffer for datalink layer */
    size_t rx_buffer_size = handle->config->memory.rx_buffer_size;
    uint8_t* rx_buffer = (uint8_t*)xgm_alloc(handle->allocator, rx_buffer_size);
    if (rx_buffer == NULL) {
        err = XGL_ERR_NO_MEMORY;
        goto cleanup_route_table;
    }

    /* Initialize data link layer */
    xgl_datalink_config_t datalink_config = {
        .rx_cache = rx_buffer,
        .rx_cache_size = handle->config->protocol.max_frame_size,
        .source_id = handle->config->source_id,
        .stats = &handle->stats.datalink,
        .rx_header_crc_errors = &handle->stats.rx_header_crc_errors,
        .rx_crc16_errors = &handle->stats.rx_crc16_errors,
        .upper_layer = NULL, /* Will be set after network layer init */
        .error_callback = handle->config->error_callback,
        .callback_user_data = handle->config->callback_user_data,
        .owner_handle = handle,
        .allocator = handle->memory->scratch,
        .auth_required = handle->config->auth_required,
        .auth_provider = handle->config->auth_provider};
    err = xgl_datalink_init(&handle->layers.datalink_ctx, &datalink_config);
    if (err != XGL_OK) {
        goto cleanup_rx_buffer;
    }

    err = xgl_instance_init_links(handle);
    if (err != XGL_OK)
        goto cleanup_datalink;

    /* Initialize network layer */
    xgl_network_config_t network_config = {
        .local_id = handle->config->source_id,
        .route_table = &handle->route_table,
        .upper_layer = NULL, /* Will be set after transport layer init */
        .lower_layer = NULL, /* Will be set after creating datalink interface */
        .error_callback = handle->config->error_callback,
        .callback_user_data = handle->config->callback_user_data,
        .stats = &handle->stats.network,
        .auth_required = handle->config->auth_required,
        .auth_provider = handle->config->auth_provider,
        .allocator = handle->memory->scratch};
#if XGL_FEATURE_AUTH
    network_config.security = &handle->layers.datalink_ctx.security;
#endif
    err = xgl_network_init(&handle->layers.network_ctx, &network_config);
    if (err != XGL_OK) {
        goto cleanup_datalink;
    }

    /* Initialize transport layer */
    xgl_transport_config_t transport_config = {
        .local_id = handle->config->source_id,
        .max_retry_count = handle->config->protocol.max_retry_count,
        .default_timeout_ms = handle->config->protocol.ack_timeout_ms,
        .window_size = handle->config->protocol.window_size,
        .enable_fragmentation = handle->config->features.enable_fragmentation,
        .max_frame_size = handle->config->protocol.max_frame_size,
        .auth_tag_len = (handle->config->auth_required &&
                         handle->config->auth_provider != NULL)
                            ? (uint8_t)handle->config->auth_provider->tag_len
                            : 0U,
        .route_table = &handle->route_table,
        .lower_layer = NULL, /* Will be set after creating network interface */
        .rx_callback = handle->config->rx_callback,
        .rx_accept_callback = handle->config->rx_accept_callback,
        .error_callback = handle->config->error_callback,
        .callback_user_data = handle->config->callback_user_data,
        .stats = &handle->stats.transport,
        .tx_retries = &handle->stats.tx_retries,
        .allocator = handle->allocator,
        .peer_idle_timeout_ms = handle->config->features.peer_idle_timeout_ms,
        .max_reassembly_slots = handle->config->features.max_reassembly_slots,
        .max_peers = handle->config->features.max_peers,
        .max_message_size = handle->config->features.max_message_size,
        .max_tx_packets = handle->config->features.max_tx_packets,
        .max_rx_buffered_packets =
            handle->config->features.max_rx_buffered_packets,
        .max_reassembly_bytes = handle->config->features.max_reassembly_bytes,
        .max_tx_message_bytes = handle->config->features.max_tx_message_bytes,
        .memory = handle->memory,
    };
    err = xgl_transport_init(&handle->layers.transport_ctx, &transport_config);
    if (err != XGL_OK) {
        goto cleanup_network;
    }

    /* Create layer interfaces */
    err = xgl_datalink_get_interface(&handle->layers.datalink_ctx,
                                     &handle->layers.datalink_iface);
    if (err != XGL_OK) {
        goto cleanup_transport;
    }

    err = xgl_network_get_interfaces(&handle->layers.network_ctx,
                                     &handle->layers.network_packet_iface,
                                     &handle->layers.network_frame_iface);
    if (err != XGL_OK) {
        goto cleanup_transport;
    }

    err = xgl_transport_get_interface(&handle->layers.transport_ctx,
                                      &handle->layers.transport_iface);
    if (err != XGL_OK) {
        goto cleanup_transport;
    }

    /* Wire up layer interfaces */
    /* Datalink -> Network -> Transport -> Application */
    handle->layers.datalink_ctx.upper_layer =
        &handle->layers.network_frame_iface;
    handle->layers.network_ctx.lower_layer = &handle->layers.datalink_iface;
    handle->layers.network_ctx.upper_layer = &handle->layers.transport_iface;
    handle->layers.transport_ctx.lower_layer =
        &handle->layers.network_packet_iface;

    /* Mark as initialized */
    handle->initialized = true;

    /* Initialize codec registry and register user-provided codecs */

    return XGL_OK;

    /* Cleanup on error -- each label destroys the layer that was successfully
     * initialized *before* the failure point.                            */

cleanup_transport:
    xgl_transport_destroy(&handle->layers.transport_ctx);

cleanup_network:
    /* Network context is embedded; no heap resources to release */
    memset(&handle->layers.network_ctx, 0, sizeof(handle->layers.network_ctx));

cleanup_datalink:
    xgl_instance_destroy_links(handle);

cleanup_rx_buffer:
    memset(&handle->layers.datalink_ctx, 0,
           sizeof(handle->layers.datalink_ctx));
    xgm_free(handle->allocator, rx_buffer);

cleanup_route_table:
    xgl_route_table_destroy(&handle->route_table);

cleanup:

    return err;
}
