/**
 * \file            xgl_instance.c
 * \brief           Protocol instance management implementation
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <datalink/xgl_parser.h>
#include <network/xgl_route.h>
#include <transport/xgl_rtt.h>
#include <transport/xgl_window.h>
#include <xgl/xgl.h>
#include <xgl/xgl_alloc.h>
#include <xgl/xgl_runtime.h>

#include "internal/xgl_allocator.h"
#include <string.h>

#include "xgl_workspace_internal.h"

/**
 * \brief           Unwind complete or partially initialized instance resources
 */
static void instance_release_resources(xgl_handle_t handle) {
    xgl_transport_destroy(&handle->layers.transport_ctx);
    xgl_instance_destroy_links(handle);
    xgl_route_table_destroy(&handle->route_table);
    memset(&handle->layers, 0, sizeof(handle->layers));
#if XGL_FEATURE_AUTH
    memset(&handle->security, 0, sizeof(handle->security));
#endif
    handle->initialized = false;
}

/*---------------------------------------------------------------------------*/
/* Instance Initialization                                                   */
/*---------------------------------------------------------------------------*/

/** \brief           Configure and bind all layers before publishing ready. */
static xgl_error_t instance_start(xgl_handle_t handle) {
    const xgl_config_t* config = handle->config;
#if XGL_FEATURE_ADVANCED_RUNTIME
    const xgl_instance_options_t* options = config->options;
#endif

#if XGL_FEATURE_STATISTICS
    /* Initialize statistics */
    const size_t reserved_bytes = handle->stats.memory_used;
    memset(&handle->stats, 0, sizeof(xgl_statistics_t));
    handle->stats.min_rtt_ms = UINT32_MAX;
    handle->stats.memory_used = reserved_bytes;
    handle->stats.memory_peak = reserved_bytes;
#endif

    /* Initialize route table */
    xgl_error_t err = xgl_route_table_init(
        &handle->route_table, config->route_table_len, handle->allocator);
    if (err != XGL_OK) {
        goto cleanup;
    }

    /* Load routes from configuration */
    if (config->route_table_len > 0) {
        err = xgl_route_table_load(&handle->route_table, config->route_table,
                                   config->route_table_len);
        if (err != XGL_OK) {
            goto cleanup;
        }
    }

    err = xgl_instance_init_links(handle);
    if (err != XGL_OK) {
        goto cleanup;
    }
#if XGL_FEATURE_AUTH
    err = xgl_security_init(&handle->security, config->source_id,
                            config->auth_required, config->auth_provider);
    if (err != XGL_OK) {
        goto cleanup;
    }
#if XGL_FEATURE_ADVANCED_RUNTIME
    handle->security.protect_installed_scopes =
        options != NULL && options->protect_installed_scopes;
#endif
#endif

    /* Initialize data link layer */
    xgl_datalink_config_t datalink_config = {
        .stats = XGL_INSTANCE_STAT(handle, datalink),
        .rx_header_crc_errors = XGL_INSTANCE_STAT(handle, rx_header_crc_errors),
        .rx_crc16_errors = XGL_INSTANCE_STAT(handle, rx_crc16_errors),
        .upper_layer = NULL, /* Will be set after network layer init */
        .error_callback = config->error_callback,
        .callback_user_data = config->callback_user_data,
        .owner_handle = handle,
        .allocator = handle->memory->scratch,
    };
#if XGL_FEATURE_AUTH
    datalink_config.security = &handle->security;
#endif
    err = xgl_datalink_init(&handle->layers.datalink_ctx, &datalink_config);
    if (err != XGL_OK) {
        goto cleanup;
    }

    /* Initialize network layer */
    xgl_network_config_t network_config = {
        .local_id = config->source_id,
        .max_frame_size = config->protocol.max_frame_size,
        .route_table = &handle->route_table,
        .upper_layer = NULL, /* Will be set after transport layer init */
        .lower_layer = NULL, /* Will be set after creating datalink interface */
        .error_callback = config->error_callback,
        .callback_user_data = config->callback_user_data,
        .stats = XGL_INSTANCE_STAT(handle, network),
        .allocator = handle->memory->scratch};
#if XGL_FEATURE_AUTH
    network_config.security = &handle->security;
#endif
    err = xgl_network_init(&handle->layers.network_ctx, &network_config);
    if (err != XGL_OK) {
        goto cleanup;
    }

    /* Initialize transport layer */
    xgl_transport_config_t transport_config = {
        .local_id = config->source_id,
        .max_retry_count = config->protocol.max_retry_count,
        .default_timeout_ms = config->protocol.ack_timeout_ms,
        .window_size = config->protocol.window_size,
        .max_frame_size = config->protocol.max_frame_size,
        .lower_layer = NULL, /* Will be set after creating network interface */
        .rx_callback = config->rx_callback,
        .rx_user_data = config->rx_user_data,
        .error_callback = config->error_callback,
        .callback_user_data = config->callback_user_data,
        .stats = XGL_INSTANCE_STAT(handle, transport),
        .tx_retries = XGL_INSTANCE_STAT(handle, tx_retries),
        .allocator = handle->allocator,
        .peer_idle_timeout_ms = config->features.peer_idle_timeout_ms,
        .max_peers = config->features.max_peers,
        .max_tx_packets = config->features.max_tx_packets,
        .max_rx_buffered_packets = config->features.max_rx_buffered_packets,
        .memory = handle->memory,
    };
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (options != NULL) {
        transport_config.tx_lifetime_ms = options->tx_lifetime_ms;
        transport_config.scope_event_callback = options->scope_event_callback;
        transport_config.extended_user_data = options->user_data;
    }
#endif
    err = xgl_transport_init(&handle->layers.transport_ctx, &transport_config);
    if (err != XGL_OK) {
        goto cleanup;
    }

    /* Create layer interfaces */
    err = xgl_datalink_get_tx_endpoint(&handle->layers.datalink_ctx,
                                       &handle->layers.datalink_tx);
    if (err != XGL_OK) {
        goto cleanup;
    }

    err = xgl_network_get_endpoints(&handle->layers.network_ctx,
                                    &handle->layers.network_tx,
                                    &handle->layers.network_rx);
    if (err != XGL_OK) {
        goto cleanup;
    }

    err = xgl_transport_get_rx_endpoint(&handle->layers.transport_ctx,
                                        &handle->layers.transport_rx);
    if (err != XGL_OK) {
        goto cleanup;
    }

    /* Wire up layer interfaces */
    /* Datalink -> Network -> Transport -> Application */
    handle->layers.datalink_ctx.upper_layer = &handle->layers.network_rx;
    handle->layers.network_ctx.lower_layer = &handle->layers.datalink_tx;
    handle->layers.network_ctx.upper_layer = &handle->layers.transport_rx;
    handle->layers.transport_ctx.lower_layer = &handle->layers.network_tx;

    /* Mark as initialized */
    handle->initialized = true;

    return XGL_OK;

cleanup:
    instance_release_resources(handle);
    return err;
}

/** \brief           Publish after startup or release an owned workspace. */
static xgl_error_t start_prepared(xgl_handle_t prepared, xgl_handle_t* handle) {
    const xgl_error_t error = instance_start(prepared);
    if (error == XGL_OK) {
        *handle = prepared;
    } else if (prepared->storage_allocator != NULL) {
        xgl_free(prepared->storage_allocator, prepared);
    }
    return error;
}

xgl_error_t xgl_init_static(const xgl_config_t* config, void* storage,
                            size_t storage_size, xgl_handle_t* handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *handle = NULL;
    if (storage == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_handle_t prepared;
    const xgl_error_t error = xgl_workspace_prepare_instance(
        config, storage, storage_size, NULL, &prepared);
    return error == XGL_OK ? start_prepared(prepared, handle) : error;
}

xgl_error_t xgl_init_allocated(const xgl_config_t* config,
                               const xgl_allocator_t* allocator,
                               xgl_handle_t* handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *handle = NULL;

    xgl_handle_t prepared;
    const xgl_error_t error =
        xgl_workspace_prepare_instance(config, NULL, 0U, allocator, &prepared);
    return error == XGL_OK ? start_prepared(prepared, handle) : error;
}

/**
 * \brief           Release protocol resources and an owned workspace exactly
 * once
 */
void xgl_destroy(xgl_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    instance_release_resources(handle);
    if (handle->storage_allocator != NULL) {
        xgl_free(handle->storage_allocator, handle);
    }
}
