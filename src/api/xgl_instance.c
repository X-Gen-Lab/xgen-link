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
#if XGL_ALLOW_FALLBACK_MALLOC
#include <xgen/memory/libc_allocator.h>
#endif

#include "xgl_instance_internal.h"

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

#if XGL_FEATURE_STATISTICS
    /* Initialize statistics */
    const size_t reserved_bytes = handle->stats.memory_used;
    memset(&handle->stats, 0, sizeof(xgl_statistics_t));
    handle->stats.min_rtt_ms = UINT32_MAX;
    handle->stats.memory_used = reserved_bytes;
    handle->stats.memory_peak = reserved_bytes;
#endif

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
            goto cleanup;
        }
    }

    err = xgl_instance_init_links(handle);
    if (err != XGL_OK) {
        goto cleanup;
    }
#if XGL_FEATURE_AUTH
    err = xgl_security_init(&handle->security, handle->config->source_id,
                            handle->config->auth_required,
                            handle->config->auth_provider);
    if (err != XGL_OK) {
        goto cleanup;
    }
#endif

    /* Initialize data link layer */
    xgl_datalink_config_t datalink_config = {
        .source_id = handle->config->source_id,
        .stats = XGL_INSTANCE_STAT(handle, datalink),
        .rx_header_crc_errors = XGL_INSTANCE_STAT(handle, rx_header_crc_errors),
        .rx_crc16_errors = XGL_INSTANCE_STAT(handle, rx_crc16_errors),
        .upper_layer = NULL, /* Will be set after network layer init */
        .error_callback = handle->config->error_callback,
        .callback_user_data = handle->config->callback_user_data,
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
        .local_id = handle->config->source_id,
        .max_frame_size = handle->config->protocol.max_frame_size,
        .route_table = &handle->route_table,
        .upper_layer = NULL, /* Will be set after transport layer init */
        .lower_layer = NULL, /* Will be set after creating datalink interface */
        .error_callback = handle->config->error_callback,
        .callback_user_data = handle->config->callback_user_data,
        .stats = XGL_INSTANCE_STAT(handle, network),
        .auth_required = handle->config->auth_required,
        .auth_provider = handle->config->auth_provider,
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
        .stats = XGL_INSTANCE_STAT(handle, transport),
        .tx_retries = XGL_INSTANCE_STAT(handle, tx_retries),
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
        goto cleanup;
    }

    /* Create layer interfaces */
    err = xgl_datalink_get_interface(&handle->layers.datalink_ctx,
                                     &handle->layers.datalink_iface);
    if (err != XGL_OK) {
        goto cleanup;
    }

    err = xgl_network_get_interfaces(&handle->layers.network_ctx,
                                     &handle->layers.network_packet_iface,
                                     &handle->layers.network_frame_iface);
    if (err != XGL_OK) {
        goto cleanup;
    }

    err = xgl_transport_get_interface(&handle->layers.transport_ctx,
                                      &handle->layers.transport_iface);
    if (err != XGL_OK) {
        goto cleanup;
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

    return XGL_OK;

cleanup:
    instance_release_resources(handle);
    return err;
}

/**
 * \brief           Reserve one complete workspace through an explicit backend
 * \details         The immutable configuration is borrowed until destruction.
 *                  Protocol initialization is deferred to xgl_init().
 */
xgl_handle_t xgl_create_checked(const xgl_config_t* config, size_t config_size,
                                uint32_t abi_version,
                                uint32_t build_config_id) {
    xgl_memory_requirements_t requirements;
    xgl_error_t err = xgl_memory_requirements_checked(
        config, config_size, abi_version, build_config_id, &requirements);
    if (err != XGL_OK) {
        return NULL;
    }

    const xgm_allocator_t* allocator = config->memory.allocator;
#if XGL_ALLOW_FALLBACK_MALLOC
    if (allocator == NULL) {
        allocator = xgm_allocator_libc();
    }
#endif
    if (!xgm_allocator_is_valid(allocator)) {
        return NULL;
    }

    void* storage = xgm_alloc(allocator, requirements.size);
    if (storage == NULL) {
        return NULL;
    }
    xgl_handle_t handle;
    err = xgl_workspace_prepare(config, storage, requirements.size, allocator,
                                &handle);
    if (err != XGL_OK) {
        xgm_free(allocator, storage);
        return NULL;
    }
    return handle;
}

/**
 * \brief           Release protocol resources and an owned workspace exactly once
 */
void xgl_destroy(xgl_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    instance_release_resources(handle);
    if (!handle->caller_owned) {
        xgm_free(handle->storage_allocator, handle);
    }
}
