/**
 * \file            xgl_transport.c
 * \brief           Transport lifecycle and layer entry points
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "internal/xgl_allocator.h"
#include "xgl/xgl_config.h"
#include "xgl_transport_internal.h"
#include <internal/xgl_diagnostics.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Transport Layer Initialization                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport with explicit bounded resource services
 * \details         Borrow the configuration and allocator descriptors.
 */
xgl_error_t xgl_transport_init(xgl_transport_ctx_t* ctx,
                               const xgl_transport_config_t* config) {
    if (!ctx || !config) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!xgl_allocator_is_valid(config->allocator)) {
        return XGL_ERR_NULL_POINTER;
    }
#if XGL_FEATURE_STATISTICS
    if (config->stats == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
#endif
    if (config->default_timeout_ms > INT32_MAX || config->window_size == 0U ||
        config->window_size > XGL_BUILD_WINDOW_CAPACITY ||
        config->max_tx_packets == 0U || config->max_peers == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

#if XGL_FEATURE_OUT_OF_ORDER
    if (config->window_size > 1U && config->max_rx_buffered_packets == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif

#if XGL_FEATURE_ADVANCED_RUNTIME
    if (config->tx_lifetime_ms > INT32_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif

    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
#if XGL_FEATURE_STATISTICS
    ctx->rtt_min_ms = UINT32_MAX;
#endif
#if XGL_FEATURE_ADVANCED_RUNTIME
    ctx->tx_lifetime_ms = config->tx_lifetime_ms;
    ctx->scope_event_callback = config->scope_event_callback;
    ctx->extended_user_data = config->extended_user_data;
#endif
    ctx->local_id = config->local_id;
    ctx->max_retry_count = config->max_retry_count;
    ctx->window_size = config->window_size;
    ctx->max_peers = config->max_peers;
    ctx->default_timeout_ms = config->default_timeout_ms;
    ctx->lower_layer = config->lower_layer;
    ctx->rx_callback = config->rx_callback;
    ctx->rx_user_data = config->rx_user_data;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
#if XGL_FEATURE_STATISTICS
    ctx->stats = config->stats;
    ctx->tx_retries = config->tx_retries;
#endif
    ctx->max_tx_packets = config->max_tx_packets;
#if XGL_FEATURE_OUT_OF_ORDER
    ctx->max_rx_buffered_packets = config->max_rx_buffered_packets;
    ctx->max_frame_size = config->max_frame_size;
#endif
    ctx->memory.peer = config->allocator;
    ctx->memory.tx_packet = config->allocator;
    ctx->memory.tx_payload = config->allocator;
    ctx->memory.scratch = config->allocator;
#if XGL_FEATURE_OUT_OF_ORDER
    ctx->memory.rx_packet = config->allocator;
    ctx->memory.rx_payload = config->allocator;
#endif
    if (config->memory != NULL) {
        ctx->memory = *config->memory;
        if (!xgl_allocator_is_valid(ctx->memory.peer) ||
            !xgl_allocator_is_valid(ctx->memory.tx_packet) ||
            !xgl_allocator_is_valid(ctx->memory.tx_payload)) {
            return XGL_ERR_INVALID_PARAM;
        }
#if XGL_FEATURE_OUT_OF_ORDER
        if (config->max_rx_buffered_packets > 0U &&
            (!xgl_allocator_is_valid(ctx->memory.rx_packet) ||
             !xgl_allocator_is_valid(ctx->memory.rx_payload))) {
            return XGL_ERR_INVALID_PARAM;
        }
#endif
    }
    ctx->peer_idle_timeout_ms = config->peer_idle_timeout_ms;

    return XGL_OK;
}

/**
 * \brief           Destroy transport layer context
 */
void xgl_transport_destroy(xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    transport_destroy_peers(ctx);

    /* Clear context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
}

/**
 * \brief           Deliver a borrowed network packet to transport
 */
static xgl_error_t transport_receive_impl(void* ctx, xgl_handle_t handle,
                                          const xgl_packet_t* packet) {
    return xgl_transport_receive((xgl_transport_ctx_t*)ctx, handle, packet);
}

/**
 * \brief           Initialize the typed transport receive boundary
 * \details         Initialize only the receive operation of the typed
 *                  endpoint.
 */
xgl_error_t xgl_transport_get_rx_endpoint(xgl_transport_ctx_t* ctx,
                                          xgl_packet_rx_endpoint_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_packet_rx_endpoint_init(iface, ctx, transport_receive_impl);
    return XGL_OK;
}

/**
 * \brief           Record a transport send failure
 */
void transport_count_send_error(xgl_transport_ctx_t* ctx) {
#if XGL_FEATURE_STATISTICS
    if (ctx != NULL && ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
#else
    (void)ctx;
#endif
}

/**
 * \brief           Report error through error callback
 */
void xgl_transport_report_error(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                xgl_error_t error, const char* message) {
    (void)message;
    if (!ctx) {
        return;
    }

    if (ctx->error_callback) {
        ctx->error_callback(handle, error, XGL_ERROR_MESSAGE(message),
                            ctx->callback_user_data);
    }
}
