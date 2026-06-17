/**
 * \file            xgl_transport_runtime.c
 * \brief           Transport runtime processing and utility APIs
 */

#include "xgl_transport_internal.h"

/**
 * \brief           Periodic transport layer processing
 */
xgl_error_t xgl_transport_run(xgl_transport_ctx_t *ctx, xgl_handle_t handle,
                              uint32_t current_time_ms)
{
    if (!ctx) {
        return XGL_ERR_NULL_POINTER;
    }

    uint32_t retransmit_count =
        transport_process_retransmissions(ctx, handle, current_time_ms);

    /* Update retransmission statistics */
    if (retransmit_count > 0 && ctx->tx_retries != NULL) {
        (*ctx->tx_retries) += retransmit_count;
    }

    /* Process fragment reassembly timeouts */
    if (ctx->fragment_mgr) {
        uint32_t timeout_count =
            xgl_fragment_process_timeouts(ctx->fragment_mgr, current_time_ms);
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

/**
 * \brief           Get next packet number for target
 */
uint32_t xgl_transport_get_next_packet_number(xgl_transport_ctx_t *ctx)
{
    if (!ctx) {
        return 0;
    }
    return xgl_window_get_next_packet_number(&ctx->window);
}

/**
 * \brief           Check if transport layer can send
 */
bool xgl_transport_can_send(const xgl_transport_ctx_t *ctx)
{
    if (!ctx) {
        return false;
    }
    return xgl_window_can_send_packet_number(&ctx->window);
}

/**
 * \brief           Report error through error callback
 */
void xgl_transport_report_error(xgl_transport_ctx_t *ctx, xgl_handle_t handle,
                                xgl_error_t error, const char *message)
{
    if (!ctx) {
        return;
    }

    if (ctx->error_callback) {
        ctx->error_callback(handle, error, message, ctx->callback_user_data);
    }
}
