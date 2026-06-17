/**
 * \file            xgl_transport_send.c
 * \brief           Transport send path implementation
 */

#include "xgl_transport_send_internal.h"

static void transport_count_send_error(xgl_transport_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

static xgl_transport_peer_state_t* transport_select_tx_peer(xgl_transport_ctx_t* ctx,
                                                            const xgl_tx_data_t* tx_data) {
    bool has_tx_scope =
        (tx_data->connection_id != 0U || tx_data->session_epoch != 0U);

    if (has_tx_scope) {
        return transport_get_or_create_peer_scope(ctx,
                                                  tx_data->target_id,
                                                  tx_data->connection_id,
                                                  tx_data->session_epoch);
    }

    return transport_get_or_create_peer(ctx, tx_data->target_id);
}

static xgl_error_t transport_prepare_reliable_send(xgl_transport_ctx_t* ctx,
                                                   xgl_handle_t handle,
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

    if (!xgl_window_can_send_packet_number(&(*peer)->tx_window)) {
        return XGL_ERR_WINDOW_FULL;
    }

    if ((*peer)->hello_sent) {
        return XGL_OK;
    }

    xgl_error_t err = transport_send_control(ctx,
                                             handle,
                                             tx_data->target_id,
                                             XGL_TRANSPORT_CONTROL_HELLO,
                                             0,
                                             (*peer)->session_id,
                                             (*peer)->connection_id,
                                             (*peer)->session_epoch);
    if (err == XGL_OK) {
        (*peer)->hello_sent = true;
    }

    return err;
}

static xgl_error_t transport_send_single_frame(xgl_transport_ctx_t* ctx,
                                               xgl_handle_t handle,
                                               xgl_transport_peer_state_t* peer,
                                               const xgl_tx_data_t* tx_data) {
    uint32_t packet_number = 0U;
    if (tx_data->reliable && peer != NULL) {
        packet_number = xgl_window_get_next_packet_number(&peer->tx_window);
    }

    xgl_reliable_packet_t* rel_packet = NULL;
    xgl_error_t err = transport_queue_reliable_tx(ctx,
                                                  peer,
                                                  tx_data,
                                                  tx_data->data,
                                                  tx_data->data_len,
                                                  packet_number,
                                                  false,
                                                  NULL,
                                                  0U,
                                                  &rel_packet);
    if (err != XGL_OK) {
        return err;
    }

    return transport_send_packet_view(ctx,
                                      handle,
                                      peer,
                                      tx_data,
                                      tx_data->data,
                                      tx_data->data_len,
                                      packet_number,
                                      false,
                                      NULL,
                                      0U,
                                      &rel_packet);
}

xgl_error_t xgl_transport_send(xgl_transport_ctx_t* ctx,
                               xgl_handle_t handle,
                               const xgl_tx_data_t* tx_data) {
    if (ctx == NULL || tx_data == NULL || tx_data->data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tx_data->data_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        if (ctx->error_callback != NULL) {
            ctx->error_callback(handle,
                                XGL_ERR_INVALID_PARAM,
                                "Transport layer not connected to network layer",
                                ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_transport_peer_state_t* peer = NULL;
    xgl_error_t err = transport_prepare_reliable_send(ctx, handle, tx_data, &peer);
    if (err != XGL_OK) {
        return err;
    }

    xgl_transport_send_plan_t send_plan;
    err = transport_build_send_plan(ctx, tx_data, &send_plan);
    if (err != XGL_OK) {
        transport_count_send_error(ctx);
        if (err == XGL_ERR_INVALID_PARAM && ctx->error_callback != NULL) {
            ctx->error_callback(handle,
                                XGL_ERR_INVALID_PARAM,
                                "max_frame_size too small for headers",
                                ctx->callback_user_data);
        }
        return err;
    }

    if (send_plan.needs_fragmentation) {
        err = transport_send_fragmented(ctx, handle, peer, tx_data, &send_plan);
    } else {
        err = transport_send_single_frame(ctx, handle, peer, tx_data);
    }

    if (err != XGL_OK) {
        return err;
    }

    ctx->stats->tx_packets++;
    ctx->stats->tx_bytes += tx_data->data_len;
    return XGL_OK;
}
