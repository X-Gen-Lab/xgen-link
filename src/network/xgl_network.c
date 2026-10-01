/**
 * \file            xgl_network.c
 * \brief           Network layer implementation
 * \author          X-Gen Lab
 */

#include <wire/xgl_wire.h>
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

    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_network_ctx_t));
    ctx->local_id = config->local_id;
    ctx->max_frame_size = config->max_frame_size;
    ctx->route_table = config->route_table;
    ctx->upper_layer = config->upper_layer;
    ctx->lower_layer = config->lower_layer;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
#if XGL_FEATURE_STATISTICS
    ctx->stats = config->stats;
#endif
#if XGL_FEATURE_AUTH
    ctx->security = config->security;
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

    /* Zero is reserved for both wire addresses. */
    if (source_id == 0U || target_id == 0U) {
        return false;
    }

    if (source_id == target_id && target_id != XGL_BROADCAST_ID) {
        return false;
    }

    return true;
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
    return xgl_network_receive_view(ctx, handle, message->view);
}

/**
 * \brief           Bind the packet and frame boundaries of a network context
 */
xgl_error_t xgl_network_get_endpoints(xgl_network_ctx_t* ctx,
                                      xgl_packet_tx_endpoint_t* packets,
                                      xgl_frame_rx_endpoint_t* frames) {
    if (ctx == NULL || packets == NULL || frames == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_packet_tx_endpoint_init(packets, ctx, network_send_impl);
    xgl_frame_rx_endpoint_init(frames, ctx, network_receive_impl);
    return XGL_OK;
}
