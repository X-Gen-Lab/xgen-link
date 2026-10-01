/**
 * \file            xgl_transport_send.c
 * \brief           Transport send path implementation
 */

#include "xgl/xgl_config.h"
#include "xgl_transport_send_internal.h"

/**
 * \brief           Select the single owner for a reliable transmit scope
 * \param[in,out]   ctx: Transport context
 * \param[in]       tx_data: Borrowed application request
 * \return          Matching peer, or NULL on resource exhaustion
 */
static xgl_transport_peer_state_t*
transport_select_tx_peer(xgl_transport_ctx_t* ctx,
                         const xgl_tx_data_t* tx_data) {
    return transport_get_or_create_peer_scope(ctx, tx_data->target_id,
                                              tx_data->connection_id,
                                              tx_data->session_epoch);
}

/**
 * \brief           Validate peer capacity and announce a reliable scope
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       tx_data: Borrowed application request
 * \param[out]      peer: Exact reliable owner, or NULL for unreliable traffic
 * \return          XGL_OK or admission error
 */
static xgl_error_t
transport_prepare_reliable_send(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                const xgl_tx_data_t* tx_data,
                                xgl_transport_peer_state_t** peer) {
    *peer = NULL;

    if (!tx_data->reliable) {
        return XGL_OK;
    }

    *peer = transport_select_tx_peer(ctx, tx_data);
    if (*peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    if ((*peer)->failed) {
        return XGL_ERR_ACK_TIMEOUT;
    }
#if XGL_FEATURE_FRAGMENTATION
    if ((*peer)->tx_message.data != NULL) {
        return XGL_ERR_BUSY;
    }
#endif
    if ((*peer)->tx_window.next_packet_number == UINT32_MAX) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    if (!xgl_window_can_send_packet_number(&(*peer)->tx_window)) {
        return XGL_ERR_WINDOW_FULL;
    }

    if ((*peer)->hello_sent) {
        return XGL_OK;
    }

    xgl_error_t err = transport_send_control(
        ctx, handle, tx_data->target_id, XGL_TRANSPORT_CONTROL_HELLO,
        (*peer)->connection_id, (*peer)->session_epoch);
    if (err == XGL_OK) {
        (*peer)->hello_sent = true;
    }

    return err;
}

xgl_error_t xgl_transport_send(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                               const xgl_tx_data_t* tx_data) {
    if (ctx == NULL || tx_data == NULL || tx_data->data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tx_data->compression_id != 0U) {
        return XGL_ERR_UNSUPPORTED;
    }

    if (tx_data->data_len == 0U || tx_data->timeout_ms > INT32_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

#if XGL_FEATURE_FRAGMENTATION
    if (ctx->enable_fragmentation &&
        tx_data->data_len > ctx->max_message_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
#endif

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        if (ctx->error_callback != NULL) {
            ctx->error_callback(
                handle, XGL_ERR_INVALID_PARAM,
                "Transport layer not connected to network layer",
                ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_transport_send_plan_t send_plan;
    xgl_error_t err = transport_build_send_plan(ctx, tx_data, &send_plan);
    if (err != XGL_OK) {
        transport_count_send_error(ctx);
        if (err == XGL_ERR_INVALID_PARAM && ctx->error_callback != NULL) {
            ctx->error_callback(handle, XGL_ERR_INVALID_PARAM,
                                "max_frame_size too small for headers",
                                ctx->callback_user_data);
        }
        return err;
    }

    /* Validate the complete frame budget before reserving a scope or HELLO. */
    xgl_transport_peer_state_t* peer = NULL;
    err = transport_prepare_reliable_send(ctx, handle, tx_data, &peer);
    if (err != XGL_OK) {
        return err;
    }

    const uint8_t* send_data = tx_data->data;
    size_t send_data_len = tx_data->data_len;

    if (send_plan.needs_fragmentation) {
#if XGL_FEATURE_FRAGMENTATION
        err = transport_send_fragmented(ctx, handle, peer, tx_data, &send_plan);
#else
        return XGL_ERR_BUFFER_TOO_SMALL;
#endif
    } else {
        uint32_t packet_number = 0U;
        if (tx_data->reliable && peer != NULL) {
            packet_number = xgl_window_get_next_packet_number(&peer->tx_window);
        }

        xgl_reliable_packet_t* rel_packet = NULL;
        err = transport_queue_reliable_tx(ctx, peer, tx_data, send_data,
                                          send_data_len, packet_number, false,
                                          NULL, 0U, &rel_packet);
        if (err == XGL_OK) {
            err = transport_send_packet_view(
                ctx, handle, peer, tx_data, send_data, send_data_len,
                packet_number, false, NULL, 0U, &rel_packet);
        }
    }

    if (err != XGL_OK) {
        return err;
    }

    ctx->stats->tx_packets++;
    ctx->stats->tx_bytes += tx_data->data_len;

    if (peer != NULL) {
        peer->last_active_ms = transport_now(ctx);
    }

    return XGL_OK;
}
