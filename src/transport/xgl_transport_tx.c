/**
 * \file            xgl_transport_tx.c
 * \brief           Transport transmit planning, admission and submission
 */

#include "network/xgl_route.h"
#include "wire/xgl_frame.h"
#include "wire/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl_transport_internal.h"
#include "xgl_transport_tx_internal.h"
#include <internal/xgl_diagnostics.h>
#include <string.h>

static uint16_t
transport_effective_max_frame_size(const xgl_transport_ctx_t* ctx,
                                   uint16_t target_id) {
    uint16_t max_frame_size = ctx->max_frame_size;

    if (ctx->route_table != NULL) {
        const xgl_route_item_t* route =
            xgl_route_table_lookup(ctx->route_table, target_id);
        if (route != NULL && route->max_frame_size < max_frame_size) {
            max_frame_size = route->max_frame_size;
        }
    }

    return max_frame_size;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t transport_check_data_tx_policy(const xgl_transport_ctx_t* ctx,
                                           uint16_t target_id,
                                           uint32_t connection_id,
                                           uint32_t session_epoch,
                                           size_t data_len) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
#if XGL_FEATURE_FRAGMENTATION
    if (ctx->enable_fragmentation && data_len > ctx->max_message_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
#else
    (void)data_len;
#endif
    const xgl_transport_peer_state_t* peer =
        transport_find_peer_scope(ctx, target_id, connection_id, session_epoch);
    return peer != NULL && peer->failed ? XGL_ERR_ACK_TIMEOUT : XGL_OK;
}

xgl_error_t transport_build_send_plan(const xgl_transport_ctx_t* ctx,
                                      const xgl_tx_data_t* tx_data,
                                      xgl_transport_send_plan_t* plan) {
    if (ctx == NULL || tx_data == NULL || plan == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(plan, 0, sizeof(*plan));
    uint16_t max_frame_size =
        transport_effective_max_frame_size(ctx, tx_data->target_id);
    size_t app_extensions_len =
        (tx_data->data_type != 0U) ? XGL_DATA_TYPE_EXT_SIZE : 0U;
    if (tx_data->session_epoch != 0U) {
        app_extensions_len += XGL_SESSION_EXT_SIZE;
    }

    size_t base_budget = 0U;
    if (!xgl_frame_payload_budget(max_frame_size, 0U, ctx->auth_tag_len,
                                  &base_budget)) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t app_payload_budget = 0U;
    if (!xgl_frame_payload_budget(max_frame_size, app_extensions_len,
                                  ctx->auth_tag_len, &app_payload_budget)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (tx_data->data_len <= app_payload_budget) {
        plan->fragment_payload_budget = app_payload_budget;
        plan->fragment_count = 1U;
        return XGL_OK;
    }

#if XGL_FEATURE_FRAGMENTATION
    if (!ctx->enable_fragmentation) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    plan->needs_fragmentation = true;
    if (app_payload_budget <= XGL_FRAGMENT_EXT_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    plan->fragment_payload_budget = app_payload_budget - XGL_FRAGMENT_EXT_SIZE;
    plan->fragment_count = tx_data->data_len / plan->fragment_payload_budget;
    if (tx_data->data_len % plan->fragment_payload_budget != 0U) {
        plan->fragment_count++;
    }
    return XGL_OK;
#else
    return XGL_ERR_BUFFER_TOO_SMALL;
#endif
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

xgl_error_t transport_queue_reliable_tx(
    const xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    const xgl_tx_data_t* tx_data, const uint8_t* data, size_t data_len,
    uint32_t packet_number, bool fragment, const uint8_t* extensions,
    size_t extensions_len, xgl_reliable_packet_t** rel_packet) {
    *rel_packet = NULL;

    if (!tx_data->reliable) {
        return XGL_OK;
    }

    if (transport_tx_packet_count(ctx) >= ctx->max_tx_packets) {
        return XGL_ERR_WINDOW_FULL;
    }
    xgl_error_t err = xgl_reliable_add_packet_number(
        &peer->reliable_queue, data, data_len, ctx->local_id,
        tx_data->target_id, packet_number, tx_data->data_type,
        tx_data->priority, transport_send_timeout_ms(ctx, peer, tx_data),
        rel_packet);
    if (err != XGL_OK) {
        return err;
    }

    (*rel_packet)->connection_id = tx_data->connection_id;
    (*rel_packet)->session_epoch = tx_data->session_epoch;
    (*rel_packet)->packet_type = XGL_PACKET_TYPE_DATA;
    (*rel_packet)->fragment = fragment;

    if (!fragment) {
        return XGL_OK;
    }

    (*rel_packet)->flags =
        XGL_WIRE_FLAG_FRAGMENTED | XGL_WIRE_FLAG_HAS_EXTENSIONS;
    err = xgl_reliable_set_packet_extensions(&peer->reliable_queue, *rel_packet,
                                             extensions, extensions_len);
    if (err != XGL_OK) {
        (void)xgl_reliable_remove_owned(&peer->reliable_queue, *rel_packet);
        *rel_packet = NULL;
    }

    return err;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t transport_send_packet_view(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle,
    xgl_transport_peer_state_t* peer, const xgl_tx_data_t* tx_data,
    const uint8_t* data, size_t data_len, uint32_t packet_number, bool fragment,
    uint8_t* extensions, size_t extensions_len,
    xgl_reliable_packet_t** rel_packet) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    xgl_packet_data_t packet_data = {.data_len = data_len, .data = data};

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
                           .fragment = fragment,
                           .priority = tx_data->priority,
                           .data = &packet_data,
                           .extensions = extensions,
                           .extensions_len = extensions_len};

    xgl_error_t err = xgl_packet_send(ctx->lower_layer, handle, &packet);
    if (err != XGL_OK) {
        transport_count_send_error(ctx);
        if (tx_data->reliable && peer != NULL) {
            (void)xgl_reliable_remove_owned(&peer->reliable_queue, *rel_packet);
            *rel_packet = NULL;
        }
        return err;
    }

    if (tx_data->reliable && peer != NULL) {
        xgl_window_advance_next_packet_number(&peer->tx_window);
        if (*rel_packet != NULL) {
            (*rel_packet)->send_timestamp = transport_now(ctx);
            (*rel_packet)->sent = true;
        }
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
    xgl_transport_ctx_t* ctx, const xgl_tx_data_t* tx_data,
    xgl_transport_peer_state_t** peer, bool* created) {
    *peer = transport_find_peer_scope(ctx, tx_data->target_id,
                                      tx_data->connection_id,
                                      tx_data->session_epoch);
    *created = false;

    if (*peer != NULL && (*peer)->failed) {
        return XGL_ERR_ACK_TIMEOUT;
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

    return has_capacity ? XGL_OK : XGL_ERR_WINDOW_FULL;
}

xgl_error_t transport_announce_peer(const xgl_transport_ctx_t* ctx,
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
    if (created && peer != NULL && transport_peer_can_reclaim(ctx, peer)) {
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
                XGL_ERROR_MESSAGE(
                    "Transport layer not connected to network layer"),
                ctx->callback_user_data);
        }
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_transport_send_plan_t send_plan;
    xgl_error_t err = transport_build_send_plan(ctx, tx_data, &send_plan);
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
    err = transport_prepare_reliable_send(ctx, tx_data, &peer, &created);
    if (err != XGL_OK) {
        transport_rollback_new_peer(ctx, handle, peer, created);
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
            err = transport_announce_peer(ctx, handle, peer);
            if (err != XGL_OK && rel_packet != NULL) {
                (void)xgl_reliable_remove_owned(&peer->reliable_queue,
                                                rel_packet);
                rel_packet = NULL;
            }
        }
        if (err == XGL_OK) {
            err = transport_send_packet_view(
                ctx, handle, peer, tx_data, send_data, send_data_len,
                packet_number, false, NULL, 0U, &rel_packet);
        }
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
        peer->last_active_ms = transport_now(ctx);
    }

    return XGL_OK;
}
