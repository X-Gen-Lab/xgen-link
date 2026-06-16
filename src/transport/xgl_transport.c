/**
 * \file            xgl_transport.c
 * \brief           Transport Layer Main Interface Implementation
 * \author          X-Gen Lab
 */

#include "xgl_transport_internal.h"
#include "xgl/xgl_config.h"
#include "xgl/internal/xgl_allocator.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Transport Layer Initialization                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize transport layer context
 */
xgl_error_t xgl_transport_init(xgl_transport_ctx_t* ctx,
                               const xgl_transport_config_t* config) {
    xgl_error_t err;

    if (!ctx || !config) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!config->stats) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
    ctx->local_id = config->local_id;
    ctx->max_retry_count = config->max_retry_count;
    ctx->default_timeout_ms = config->default_timeout_ms;
    ctx->enable_fragmentation = config->enable_fragmentation;
    ctx->max_frame_size = config->max_frame_size;
    ctx->auth_tag_len = config->auth_tag_len;
    ctx->route_table = config->route_table;
    ctx->next_session_id = (uint16_t)(config->local_id & XGL_SESSION_ID_MASK);
    if (ctx->next_session_id == 0U) {
        ctx->next_session_id = 1U;
    }
    ctx->lower_layer = config->lower_layer;
    ctx->rx_callback = config->rx_callback;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->stats = config->stats;
    ctx->tx_retries = config->tx_retries;
    ctx->allocator = config->allocator;

    /* Initialize RTT estimator */
    xgl_rtt_init(&ctx->rtt_est);

    /* Initialize sliding window */
    err = xgl_window_init_with_allocator(&ctx->window,
                                         config->window_size,
                                         config->allocator);
    if (err != XGL_OK) {
        return err;
    }

    /* Initialize reliable transmission queue */
    err = xgl_reliable_init(&ctx->reliable_queue, config->max_retry_count, config->allocator);
    if (err != XGL_OK) {
        xgl_window_destroy(&ctx->window);
        return err;
    }

    /* Initialize fragmentation manager if enabled */
    if (config->enable_fragmentation) {
        ctx->fragment_mgr = (xgl_fragment_manager_t*)transport_malloc(config->allocator,
                                                                       sizeof(xgl_fragment_manager_t));
        if (!ctx->fragment_mgr) {
            xgl_reliable_destroy(&ctx->reliable_queue);
            xgl_window_destroy(&ctx->window);
            return XGL_ERR_NO_MEMORY;
        }

        err = xgl_fragment_init(ctx->fragment_mgr, 8, XGL_FRAGMENT_TIMEOUT_MS, config->allocator);
        if (err != XGL_OK) {
            transport_free(config->allocator, ctx->fragment_mgr);
            ctx->fragment_mgr = NULL;
            xgl_reliable_destroy(&ctx->reliable_queue);
            xgl_window_destroy(&ctx->window);
            return err;
        }
    }

    return XGL_OK;
}

/**
 * \brief           Destroy transport layer context
 */
void xgl_transport_destroy(xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    /* Destroy fragmentation manager if allocated */
    if (ctx->fragment_mgr) {
        xgl_fragment_destroy(ctx->fragment_mgr);
        transport_free(ctx->allocator, ctx->fragment_mgr);
        ctx->fragment_mgr = NULL;
    }

    /* Destroy reliable transmission queue */
    xgl_reliable_destroy(&ctx->reliable_queue);

    /* Destroy sliding window */
    xgl_window_destroy(&ctx->window);

    transport_destroy_peers(ctx);

    /* Clear context */
    memset(ctx, 0, sizeof(xgl_transport_ctx_t));
}

/*---------------------------------------------------------------------------*/
/* Transport Layer Periodic Processing                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Periodic transport layer processing
 */
xgl_error_t xgl_transport_run(xgl_transport_ctx_t* ctx,
                              xgl_handle_t handle,
                              uint32_t current_time_ms) {
    if (!ctx) {
        return XGL_ERR_NULL_POINTER;
    }

    uint32_t retransmit_count = transport_process_retransmissions(ctx, handle, current_time_ms);

    /* Update retransmission statistics */
    if (retransmit_count > 0 && ctx->tx_retries != NULL) {
        (*ctx->tx_retries) += retransmit_count;
    }

    /* Process fragment reassembly timeouts */
    if (ctx->fragment_mgr) {
        uint32_t timeout_count = xgl_fragment_process_timeouts(ctx->fragment_mgr,
                                                                current_time_ms);
        if (timeout_count > 0) {
            /* Report error */
            if (ctx->error_callback) {
                ctx->error_callback(handle, XGL_ERR_TIMEOUT,
                                  "Fragment reassembly timeout",
                                  ctx->callback_user_data);
            }

            /* Update statistics */
            ctx->stats->rx_dropped += timeout_count;
        }
    }

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Transport Layer Utility Functions                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get next packet number for target
 */
uint32_t xgl_transport_get_next_packet_number(xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return 0;
    }
    return xgl_window_get_next_packet_number(&ctx->window);
}

/**
 * \brief           Check if transport layer can send
 */
bool xgl_transport_can_send(const xgl_transport_ctx_t* ctx) {
    if (!ctx) {
        return false;
    }
    return xgl_window_can_send_packet_number(&ctx->window);
}

/**
 * \brief           Report error through error callback
 */
void xgl_transport_report_error(xgl_transport_ctx_t* ctx,
                                xgl_handle_t handle,
                                xgl_error_t error,
                                const char* message) {
    if (!ctx) {
        return;
    }

    if (ctx->error_callback) {
        ctx->error_callback(handle, error, message, ctx->callback_user_data);
    }
}


/*---------------------------------------------------------------------------*/
/* Layer Interface Implementation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Transport layer send implementation (not used - app calls transport_send directly)
 * \details         This function would be called by application layer if using interface pattern
 */
static xgl_error_t transport_send_impl(void* ctx,
                                      xgl_handle_t handle,
                                      void* data) {
    (void)ctx;
    (void)handle;
    (void)data;
    /* Application calls xgl_transport_send directly, not through interface */
    return XGL_ERR_INVALID_PARAM;
}

/**
 * \brief           Transport layer receive implementation (called by lower layers)
 * \details         This function is called by network layer to deliver packets
 */
static xgl_error_t transport_receive_impl(void* ctx,
                                         xgl_handle_t handle,
                                         // cppcheck-suppress constParameterCallback
                                         void* data) {
    xgl_transport_ctx_t* trans_ctx = (xgl_transport_ctx_t*)ctx;
    const xgl_packet_t* packet = (const xgl_packet_t*)data;

    if (trans_ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Forward to transport receive function */
    return xgl_transport_receive(trans_ctx, handle, packet);
}

/**
 * \brief           Transport layer error reporting implementation
 * \details         This function is called to report errors to application
 */
static xgl_error_t transport_report_error_impl(void* ctx,
                                              xgl_handle_t handle,
                                              void* data) {
    xgl_transport_ctx_t* trans_ctx = (xgl_transport_ctx_t*)ctx;
    xgl_layer_error_info_t* error_info = (xgl_layer_error_info_t*)data;

    if (trans_ctx == NULL || error_info == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Forward error to callback if available */
    if (trans_ctx->error_callback != NULL) {
        trans_ctx->error_callback(handle, error_info->error,
                                 error_info->message,
                                 trans_ctx->callback_user_data);
    }

    return XGL_OK;
}

/**
 * \brief           Get transport layer interface
 * \details         Returns the layer interface for this transport instance
 * \param[in]       ctx: Transport layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_transport_get_interface(xgl_transport_ctx_t* ctx,
                                       xgl_layer_interface_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_layer_interface_init(iface,
                            ctx,
                            transport_send_impl,
                            transport_receive_impl,
                            transport_report_error_impl);

    return XGL_OK;
}
