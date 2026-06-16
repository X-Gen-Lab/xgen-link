/**
 * \file            xgl_transport_send.c
 * \brief           Transport send path implementation
 */

#include "xgl_transport_send_internal.h"
#include "xgl/internal/xgl_time.h"
#include "xgl/internal/xgl_wire.h"

static void transport_count_send_error(xgl_transport_ctx_t* ctx) {
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

static int32_t transport_send_timeout_ms(const xgl_transport_ctx_t* ctx,
                                         const xgl_transport_peer_state_t* peer,
                                         const xgl_tx_data_t* tx_data) {
    if (tx_data->timeout_ms > 0) {
        return (int32_t)tx_data->timeout_ms;
    }

    int32_t timeout_ms = (peer != NULL) ? xgl_rtt_get_rto(&peer->rtt_est) :
                                          xgl_rtt_get_rto(&ctx->rtt_est);
    if (timeout_ms == 0) {
        timeout_ms = (int32_t)ctx->default_timeout_ms;
    }

    return timeout_ms;
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

xgl_error_t transport_queue_reliable_tx(xgl_transport_ctx_t* ctx,
                                        xgl_transport_peer_state_t* peer,
                                        const xgl_tx_data_t* tx_data,
                                        const uint8_t* data,
                                        size_t data_len,
                                        uint32_t packet_number,
                                        bool fragment,
                                        const uint8_t* extensions,
                                        size_t extensions_len,
                                        xgl_reliable_packet_t** rel_packet) {
    *rel_packet = NULL;

    if (!tx_data->reliable) {
        return XGL_OK;
    }

    xgl_error_t err = xgl_reliable_add_packet_number(&peer->reliable_queue,
                                                     data,
                                                     data_len,
                                                     ctx->local_id,
                                                     tx_data->target_id,
                                                     packet_number,
                                                     tx_data->data_type,
                                                     tx_data->priority,
                                                     transport_send_timeout_ms(ctx,
                                                                               peer,
                                                                               tx_data),
                                                     NULL);
    if (err != XGL_OK) {
        return err;
    }

    *rel_packet = xgl_reliable_find_packet_number(&peer->reliable_queue,
                                                  packet_number,
                                                  tx_data->target_id);
    if (*rel_packet == NULL) {
        return XGL_OK;
    }

    (*rel_packet)->session_id = peer->session_id;
    (*rel_packet)->connection_id = tx_data->connection_id;
    (*rel_packet)->session_epoch = tx_data->session_epoch;
    (*rel_packet)->packet_type = XGL_PACKET_TYPE_DATA;
    (*rel_packet)->fragment = fragment;

    if (!fragment) {
        return XGL_OK;
    }

    (*rel_packet)->flags = XGL_WIRE_FLAG_FRAGMENTED | XGL_WIRE_FLAG_HAS_EXTENSIONS;
    err = xgl_reliable_set_packet_extensions(&peer->reliable_queue,
                                             *rel_packet,
                                             extensions,
                                             extensions_len);
    if (err != XGL_OK) {
        (void)xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                                packet_number,
                                                tx_data->target_id);
        *rel_packet = NULL;
    }

    return err;
}

xgl_error_t transport_send_packet_view(xgl_transport_ctx_t* ctx,
                                       xgl_handle_t handle,
                                       xgl_transport_peer_state_t* peer,
                                       const xgl_tx_data_t* tx_data,
                                       const uint8_t* data,
                                       size_t data_len,
                                       uint32_t packet_number,
                                       bool fragment,
                                       uint8_t* extensions,
                                       size_t extensions_len,
                                       xgl_reliable_packet_t** rel_packet) {
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = data_len,
        .data = data,
        .owned_data = NULL
    };

    xgl_packet_t packet = {
        .source_id = ctx->local_id,
        .target_id = tx_data->target_id,
        .packet_number = packet_number,
        .session_id = (peer != NULL) ? peer->session_id : 0,
        .connection_id = tx_data->connection_id,
        .session_epoch = tx_data->session_epoch,
        .data_type = tx_data->data_type,
        .reliable = tx_data->reliable,
        .fragment = fragment,
        .priority = tx_data->priority,
        .data = &packet_data,
        .extensions = extensions,
        .extensions_len = extensions_len,
        .phy = NULL
    };

    xgl_error_t err = xgl_layer_send(ctx->lower_layer, handle, &packet);
    if (err != XGL_OK) {
        transport_count_send_error(ctx);
        if (tx_data->reliable && peer != NULL) {
            (void)xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                                    packet_number,
                                                    tx_data->target_id);
            *rel_packet = NULL;
        }
        return err;
    }

    if (tx_data->reliable && peer != NULL) {
        transport_commit_packet_number(ctx, peer);
        if (*rel_packet != NULL) {
            (*rel_packet)->send_timestamp = xgl_time_ms();
            if (!fragment) {
                (*rel_packet)->phy = packet.phy;
            }
        }
    }

    return XGL_OK;
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
