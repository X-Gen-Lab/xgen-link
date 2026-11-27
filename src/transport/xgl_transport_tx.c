/**
 * \file            xgl_transport_tx.c
 * \brief           Transport transmit planning, admission and submission
 */

#include "network/xgl_route.h"
#include "wire/xgl_frame.h"
#include "wire/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl_transport_internal.h"
#include <internal/xgl_diagnostics.h>

static xgl_error_t
transport_tx_scope_status(const xgl_transport_peer_state_t* peer) {
    return peer != NULL && peer->failed ? XGL_ERR_ACK_TIMEOUT : XGL_OK;
}

static xgl_error_t
transport_check_active_tx_scope(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                xgl_transport_peer_state_t* peer) {
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (peer != NULL && transport_expire_tx(ctx, handle, peer)) {
        return XGL_ERR_TIMEOUT;
    }
#else
    (void)ctx;
    (void)handle;
#endif
    return transport_tx_scope_status(peer);
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t transport_check_data_tx_policy(const xgl_transport_ctx_t* ctx,
                                           uint16_t target_id,
                                           uint32_t connection_id,
                                           uint32_t session_epoch) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    return transport_tx_scope_status(transport_find_peer_scope(
        ctx, target_id, connection_id, session_epoch));
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t transport_prepare_data_tx(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle, uint16_t target_id,
                                      uint32_t connection_id,
                                      uint32_t session_epoch) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    return transport_check_active_tx_scope(
        ctx, handle,
        transport_find_peer_scope(ctx, target_id, connection_id,
                                  session_epoch));
}

/** \brief           Check the complete frame budget without changing state. */
static xgl_error_t transport_check_frame_budget(const xgl_transport_ctx_t* ctx,
                                                const xgl_tx_data_t* tx_data) {
    const xgl_route_item_t* route =
        xgl_route_table_lookup(ctx->route_table, tx_data->target_id);
    size_t max_frame_size = xgl_route_frame_limit(route, ctx->max_frame_size);
    size_t extensions_len = xgl_wire_packet_extension_size(
        tx_data->data_type, tx_data->session_epoch);

    size_t payload_budget;
    if (!xgl_frame_payload_budget(max_frame_size, 0U, ctx->auth_tag_len,
                                  &payload_budget)) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (!xgl_frame_payload_budget(max_frame_size, extensions_len,
                                  ctx->auth_tag_len, &payload_budget) ||
        tx_data->data_len > payload_budget) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    return XGL_OK;
}

static int32_t transport_send_timeout_ms(const xgl_transport_ctx_t* ctx,
                                         const xgl_transport_peer_state_t* peer,
                                         const xgl_tx_data_t* tx_data) {
    if (tx_data->timeout_ms > 0) {
        return (int32_t)tx_data->timeout_ms;
    }

#if XGL_FEATURE_ADAPTIVE_RTO
    if (peer != NULL && xgl_rtt_is_initialized(&peer->rtt_est)) {
        return xgl_rtt_get_rto(&peer->rtt_est);
    }
#else
    (void)peer;
#endif
    return ctx->default_timeout_ms != 0U ? (int32_t)ctx->default_timeout_ms
                                         : XGL_DEFAULT_RTO_MS;
}

static xgl_error_t transport_queue_reliable_tx(
    const xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    const xgl_tx_data_t* tx_data, uint32_t packet_number,
    xgl_reliable_packet_t** rel_packet) {
    *rel_packet = NULL;

    if (!tx_data->reliable) {
        return XGL_OK;
    }

    if (transport_tx_packet_count(ctx) >= ctx->max_tx_packets) {
        return XGL_ERR_WINDOW_FULL;
    }
    xgl_error_t err = xgl_reliable_add_packet_number(
        &peer->reliable_queue, tx_data->data, tx_data->data_len, ctx->local_id,
        tx_data->target_id, packet_number, tx_data->data_type,
        tx_data->priority, transport_send_timeout_ms(ctx, peer, tx_data),
        rel_packet);
    if (err != XGL_OK) {
        return err;
    }

    (*rel_packet)->connection_id = tx_data->connection_id;
    (*rel_packet)->session_epoch = tx_data->session_epoch;
    return XGL_OK;
}

/** \brief           Submit DATA and commit its reliable number on success. */
static xgl_error_t
transport_send_packet_view(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                           xgl_transport_peer_state_t* peer,
                           const xgl_tx_data_t* tx_data, uint32_t packet_number,
                           xgl_reliable_packet_t* rel_packet) {
    xgl_packet_data_t packet_data = {.data_len = tx_data->data_len,
                                     .data = tx_data->data};

    xgl_packet_t packet = {.packet_type = XGL_PACKET_TYPE_DATA,
                           .source_id = ctx->local_id,
                           .target_id = tx_data->target_id,
                           .packet_number = packet_number,
                           .connection_id = tx_data->connection_id,
                           .session_epoch = tx_data->session_epoch,
                           .data_type = tx_data->data_type,
                           .reliable = tx_data->reliable
                                           ? XGL_RELIABILITY_ACK_ELICITING
                                           : XGL_RELIABILITY_NONE,
                           .priority = tx_data->priority,
                           .data = &packet_data};

    xgl_error_t err = transport_submit_packet(ctx, handle, &packet);
    if (err != XGL_OK) {
        transport_count_send_error(ctx);
        if (rel_packet != NULL) {
            (void)xgl_reliable_remove_owned(&peer->reliable_queue, rel_packet);
        }
        return err;
    }

    if (rel_packet != NULL) {
        xgl_window_advance_next_packet_number(&peer->tx_window);
        rel_packet->send_timestamp = transport_now(ctx);
        rel_packet->sent = true;
    }

    return XGL_OK;
}

/**
 * \brief           Check scope state and reserve reliable submission ownership
 * \param[in,out]   ctx: Transport context
 * \param[in]       tx_data: Borrowed application request
 * \param[out]      peer: Exact owner on success; NULL for unreliable traffic
 * \param[out]      created: Whether this request reserved the peer
 * \return          XGL_OK or admission error
 */
static xgl_error_t transport_prepare_reliable_send(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle, const xgl_tx_data_t* tx_data,
    xgl_transport_peer_state_t** peer, bool* created) {
    *peer = transport_find_peer_scope(ctx, tx_data->target_id,
                                      tx_data->connection_id,
                                      tx_data->session_epoch);
    *created = false;
    xgl_error_t status = transport_check_active_tx_scope(ctx, handle, *peer);
    if (status != XGL_OK) {
        return status;
    }
    if (!tx_data->reliable) {
        *peer = NULL;
        return XGL_OK;
    }

    const bool has_capacity =
        transport_tx_packet_count(ctx) < ctx->max_tx_packets;
    if (*peer == NULL) {
        if (!has_capacity) {
            return XGL_ERR_WINDOW_FULL;
        }
        *peer = transport_get_or_create_peer_scope(ctx, tx_data->target_id,
                                                   tx_data->connection_id,
                                                   tx_data->session_epoch);
        *created = *peer != NULL;
    }
    if (*peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    if ((*peer)->tx_window.next_packet_number == UINT32_MAX) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    if (!xgl_window_can_send_packet_number(&(*peer)->tx_window)) {
        return XGL_ERR_WINDOW_FULL;
    }

    return has_capacity ? XGL_OK : XGL_ERR_WINDOW_FULL;
}

static xgl_error_t transport_announce_peer(const xgl_transport_ctx_t* ctx,
                                           xgl_handle_t handle,
                                           xgl_transport_peer_state_t* peer) {
    if (peer == NULL || peer->hello_sent) {
        return XGL_OK;
    }

    xgl_error_t err = transport_send_control(
        ctx, handle, peer->peer_id, XGL_TRANSPORT_CONTROL_HELLO,
        peer->connection_id, peer->session_epoch);
    if (err == XGL_OK) {
        peer->hello_sent = true;
    }

    return err;
}

/**
 * \brief           Return a new scope that never accepted reliable data
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       peer: Scope considered for rollback
 * \param[in]       created: Whether the current request reserved this scope
 */
static void transport_rollback_new_peer(xgl_transport_ctx_t* ctx,
                                        xgl_handle_t handle,
                                        xgl_transport_peer_state_t* peer,
                                        bool created) {
    if (created && peer != NULL && transport_peer_can_reclaim(peer)) {
        (void)xgl_transport_close_scope(ctx, handle, peer->peer_id,
                                        peer->connection_id,
                                        peer->session_epoch);
    }
}

xgl_error_t xgl_transport_send(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                               const xgl_tx_data_t* tx_data) {
    if (ctx == NULL || tx_data == NULL || tx_data->data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tx_data->compression_id != 0U) {
        return XGL_ERR_UNSUPPORTED;
    }

    if (tx_data->data_len == 0U || tx_data->timeout_ms > INT32_MAX ||
        (tx_data->reliable && tx_data->target_id == UINT16_MAX)) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (ctx->lower_layer == NULL || ctx->lower_layer->send == NULL) {
        if (ctx->error_callback != NULL) {
            ctx->error_callback(
                handle, XGL_ERR_INVALID_PARAM,
                XGL_ERROR_MESSAGE(
                    "Transport layer not connected to network layer"),
                ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_error_t err = transport_check_frame_budget(ctx, tx_data);
    if (err != XGL_OK) {
        transport_count_send_error(ctx);
        if (err == XGL_ERR_INVALID_PARAM && ctx->error_callback != NULL) {
            ctx->error_callback(
                handle, XGL_ERR_INVALID_PARAM,
                XGL_ERROR_MESSAGE("max_frame_size too small for headers"),
                ctx->callback_user_data);
        }
        return err;
    }

    /* Validate the complete frame budget before reserving a scope or HELLO. */
    xgl_transport_peer_state_t* peer = NULL;
    bool created = false;
    err =
        transport_prepare_reliable_send(ctx, handle, tx_data, &peer, &created);
    if (err != XGL_OK) {
        transport_rollback_new_peer(ctx, handle, peer, created);
        return err;
    }

    uint32_t packet_number = 0U;
    if (peer != NULL) {
        packet_number = xgl_window_get_next_packet_number(&peer->tx_window);
    }

    xgl_reliable_packet_t* rel_packet = NULL;
    err = transport_queue_reliable_tx(ctx, peer, tx_data, packet_number,
                                      &rel_packet);
    if (err == XGL_OK) {
        err = transport_announce_peer(ctx, handle, peer);
        if (err != XGL_OK && rel_packet != NULL) {
            (void)xgl_reliable_remove_owned(&peer->reliable_queue, rel_packet);
        }
    }
    if (err == XGL_OK) {
        err = transport_send_packet_view(ctx, handle, peer, tx_data,
                                         packet_number, rel_packet);
    }

    if (err != XGL_OK) {
        transport_rollback_new_peer(ctx, handle, peer, created);
        return err;
    }

#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->tx_packets++;
        ctx->stats->tx_bytes += tx_data->data_len;
    }
#endif

    if (peer != NULL) {
#if XGL_FEATURE_ADVANCED_RUNTIME
        transport_note_tx_admission(ctx, peer);
#endif
        peer->last_active_ms = transport_now(ctx);
    }

    return XGL_OK;
}
