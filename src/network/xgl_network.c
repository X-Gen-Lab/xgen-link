/**
 * \file            xgl_network.c
 * \brief           Network layer implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_error.h>

#include <string.h>

#include "xgl_network_internal.h"

/*---------------------------------------------------------------------------*/
/* Public API Implementation                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize network layer context
 */
xgl_error_t xgl_network_init(xgl_network_ctx_t* ctx,
                             const xgl_network_config_t* config) {
    if (ctx == NULL || config == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (config->route_table == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
#if !XGL_FEATURE_AUTH
    if (config->auth_required) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif

    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_network_ctx_t));
    ctx->local_id = config->local_id;
    ctx->route_table = config->route_table;
    ctx->upper_layer = config->upper_layer;
    ctx->lower_layer = config->lower_layer;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->stats = config->stats;
#if XGL_FEATURE_AUTH
    ctx->auth_required = config->auth_required;
    ctx->security = config->security;
    ctx->auth_provider = config->auth_provider;
#endif
#if XGL_FEATURE_FORWARDING
    ctx->allocator = config->allocator;
#endif

    return XGL_OK;
}

/**
 * \brief           Validate packet addressing
 * \details         Checks if source and target IDs are valid
 */
bool xgl_network_validate_address(const xgl_network_ctx_t* ctx,
                                  uint16_t target_id, uint16_t source_id) {
    if (ctx == NULL) {
        return false;
    }

    /* Source ID should not be broadcast */
    if (source_id == XGL_BROADCAST_ID) {
        return false;
    }

    /* Source ID should not be zero (reserved) */
    if (source_id == 0) {
        return false;
    }

    /* Target ID can be broadcast or specific node */
    /* Zero is reserved but we allow it for special cases */

    if (source_id == target_id && target_id != XGL_BROADCAST_ID) {
        return false;
    }

    return true;
}

/**
 * \brief           Invoke error callback
 * \details         Reports error through registered callback
 */
void xgl_network_report_error(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                              xgl_error_t error, const char* message) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle, error, message, ctx->callback_user_data);
    }

    /* Update error statistics */
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

/*---------------------------------------------------------------------------*/
/* Layer Interface Implementation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Submit a typed transport packet to the network
 */
static xgl_error_t network_send_impl(void* ctx, xgl_handle_t handle,
                                     xgl_packet_t* packet) {
    return xgl_network_send_with_handle(ctx, handle, packet);
}

/**
 * \brief           Receive a borrowed frame from the datalink
 */
static xgl_error_t network_receive_impl(void* ctx, xgl_handle_t handle,
                                        const xgl_frame_rx_message_t* message) {
    if (ctx == NULL || message == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (message->view != NULL) {
        return xgl_network_receive_view(ctx, handle, message->view);
    }
    return xgl_network_receive(ctx, handle, message->frame_buf,
                               message->frame_len);
}

/**
 * \brief           Bind the packet and frame boundaries of a network context
 */
xgl_error_t xgl_network_get_interfaces(xgl_network_ctx_t* ctx,
                                       xgl_packet_interface_t* packets,
                                       xgl_frame_interface_t* frames) {
    if (ctx == NULL || packets == NULL || frames == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_packet_interface_init(packets, ctx, network_send_impl, NULL);
    xgl_frame_interface_init(frames, ctx, NULL, network_receive_impl);
    return XGL_OK;
}
