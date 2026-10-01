/**
 * \file            xgl_transport.c
 * \brief           Transport Layer Main Interface Implementation
 * \author          X-Gen Lab
 */

#include <string.h>

#include "xgen/memory/allocator.h"
#include "xgl/xgl_config.h"
#include "xgl_transport_internal.h"

/*---------------------------------------------------------------------------*/
/* Transport Layer Initialization                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport with explicit bounded resource services
 * \param[out]      ctx: Transport context
 * \param[in]       config: Borrowed configuration and allocator descriptors
 * \return          XGL_OK or configuration validation error
 */
xgl_error_t xgl_transport_init(xgl_transport_ctx_t* ctx,
                               const xgl_transport_config_t* config) {
    if (!ctx || !config) {
        return XGL_ERR_NULL_POINTER;
    }

    if (config->stats == NULL || !xgm_allocator_is_valid(config->allocator)) {
        return XGL_ERR_NULL_POINTER;
    }
    if (config->default_timeout_ms > INT32_MAX || config->window_size == 0U ||
        config->window_size > 128U || config->max_tx_packets == 0U ||
        config->max_peers == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

#if SIZE_MAX > UINT32_MAX
    if (config->max_message_size > UINT32_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif

#if XGL_FEATURE_OUT_OF_ORDER
    if (config->window_size > 1U && config->max_rx_buffered_packets == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif
#if XGL_FEATURE_FRAGMENTATION
    if (config->enable_fragmentation &&
        (config->max_tx_message_bytes == 0U ||
         config->max_reassembly_bytes == 0U || config->max_message_size == 0U ||
         config->max_reassembly_slots == 0U)) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif

    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
#if XGL_FEATURE_DIAGNOSTICS
    ctx->rtt_min_ms = UINT32_MAX;
#endif
    ctx->local_id = config->local_id;
    ctx->max_retry_count = config->max_retry_count;
    ctx->window_size = config->window_size;
    ctx->max_peers = config->max_peers;
    ctx->default_timeout_ms = config->default_timeout_ms;
#if XGL_FEATURE_FRAGMENTATION
    ctx->enable_fragmentation = config->enable_fragmentation;
#endif
    ctx->max_frame_size = config->max_frame_size;
    ctx->auth_tag_len = config->auth_tag_len;
    ctx->route_table = config->route_table;
    ctx->lower_layer = config->lower_layer;
    ctx->rx_callback = config->rx_callback;
    ctx->rx_accept_callback = config->rx_accept_callback;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->stats = config->stats;
    ctx->tx_retries = config->tx_retries;
    ctx->max_tx_packets = config->max_tx_packets;
#if XGL_FEATURE_OUT_OF_ORDER
    ctx->max_rx_buffered_packets = config->max_rx_buffered_packets;
#endif
    ctx->memory.peer = config->allocator;
    ctx->memory.window = config->allocator;
    ctx->memory.tx_packet = config->allocator;
    ctx->memory.tx_payload = config->allocator;
    ctx->memory.scratch = config->allocator;
#if XGL_FEATURE_FRAGMENTATION
    ctx->memory.tx_extensions = config->allocator;
    ctx->memory.tx_message = config->allocator;
    ctx->memory.reassembly = config->allocator;
    ctx->memory.reassembly_payload = config->allocator;
#endif
#if XGL_FEATURE_OUT_OF_ORDER
    ctx->memory.rx_packet = config->allocator;
    ctx->memory.rx_payload = config->allocator;
    ctx->memory.rx_extensions = config->allocator;
#endif
    if (config->memory != NULL) {
        ctx->memory = *config->memory;
        if (!xgm_allocator_is_valid(ctx->memory.peer) ||
            !xgm_allocator_is_valid(ctx->memory.window) ||
            !xgm_allocator_is_valid(ctx->memory.tx_packet) ||
            !xgm_allocator_is_valid(ctx->memory.tx_payload)) {
            return XGL_ERR_INVALID_PARAM;
        }
#if XGL_FEATURE_FRAGMENTATION
        if (config->enable_fragmentation &&
            (!xgm_allocator_is_valid(ctx->memory.tx_extensions) ||
             !xgm_allocator_is_valid(ctx->memory.tx_message) ||
             !xgm_allocator_is_valid(ctx->memory.reassembly) ||
             !xgm_allocator_is_valid(ctx->memory.reassembly_payload))) {
            return XGL_ERR_INVALID_PARAM;
        }
#endif
#if XGL_FEATURE_OUT_OF_ORDER
        if (config->max_rx_buffered_packets > 0U &&
            (!xgm_allocator_is_valid(ctx->memory.rx_packet) ||
             !xgm_allocator_is_valid(ctx->memory.rx_payload) ||
             !xgm_allocator_is_valid(ctx->memory.rx_extensions))) {
            return XGL_ERR_INVALID_PARAM;
        }
#endif
    }
    ctx->peer_idle_timeout_ms = config->peer_idle_timeout_ms;
#if XGL_FEATURE_FRAGMENTATION
    ctx->max_message_size = config->max_message_size;
    ctx->max_reassembly_slots = config->max_reassembly_slots;
#endif

#if XGL_FEATURE_FRAGMENTATION
    /* Initialize fragmentation manager if enabled */
    if (config->enable_fragmentation) {
        ctx->max_tx_message_bytes = config->max_tx_message_bytes;
        size_t max_reassembly_bytes = config->max_reassembly_bytes;
        ctx->fragment_mgr = &ctx->fragment_storage;
        xgl_error_t err =
            xgl_fragment_init(ctx->fragment_mgr, ctx->max_reassembly_slots,
                              XGL_FRAGMENT_TIMEOUT_MS, ctx->memory.reassembly);
        if (err != XGL_OK) {
            ctx->fragment_mgr = NULL;
            return err;
        }
        ctx->fragment_mgr->data_allocator = ctx->memory.reassembly_payload;
        err = xgl_fragment_set_limits(ctx->fragment_mgr, ctx->max_message_size,
                                      max_reassembly_bytes);
        if (err != XGL_OK) {
            xgl_fragment_destroy(ctx->fragment_mgr);
            ctx->fragment_mgr = NULL;
            return err;
        }
    }

#else
    if (config->enable_fragmentation) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif

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

#if XGL_FEATURE_FRAGMENTATION
    /* Destroy fragmentation manager after peer-owned pending deliveries. */
    if (ctx->fragment_mgr) {
        xgl_fragment_destroy(ctx->fragment_mgr);
        ctx->fragment_mgr = NULL;
    }

#endif

    /* Clear context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
}
