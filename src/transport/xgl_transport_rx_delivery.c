/**
 * \file            xgl_transport_rx_delivery.c
 * \brief           Application admission and completed-message ownership
 */

#include "wire/xgl_wire.h"
#include "xgl_fragment_internal.h"
#include "xgl_transport_internal.h"
#include "xgl_transport_rx_internal.h"
#include <internal/xgl_diagnostics.h>

/**
 * \brief           Offer borrowed payload bytes to the application
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       source_id: Remote node ID
 * \param[in]       data_type: Application message type
 * \param[in]       data: Borrowed payload bytes
 * \param[in]       len: Payload size
 * \return          Application admission result
 */
static xgl_error_t transport_accept_payload(
#if XGL_FEATURE_ADVANCED_RUNTIME
    const xgl_rx_info_t* info,
#endif
    xgl_transport_ctx_t* ctx, xgl_handle_t handle, uint16_t source_id,
    uint8_t data_type, const uint8_t* data, size_t len) {
    xgl_error_t err = XGL_OK;
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (ctx->rx_scoped_callback != NULL) {
        err = ctx->rx_scoped_callback(handle, info, data, len,
                                      ctx->extended_user_data);
    } else {
#endif
        if (ctx->rx_accept_callback != NULL) {
            err = ctx->rx_accept_callback(handle, source_id, data_type, data,
                                          len, ctx->callback_user_data);
        } else if (ctx->rx_callback != NULL) {
            ctx->rx_callback(handle, source_id, data_type, data, len,
                             ctx->callback_user_data);
        } else {
            return XGL_ERR_UNSUPPORTED;
        }
#if XGL_FEATURE_ADVANCED_RUNTIME
    }
#endif
    if (err != XGL_OK) {
        return err;
    }
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += len;
    }
#endif
    return XGL_OK;
}

#if XGL_FEATURE_FRAGMENTATION
/**
 * \brief           Check every TLV before admitting one fragment identity
 * \param[in]       ctx: Transport context with fragmentation enabled
 * \param[in]       packet: Borrowed packet with a valid payload span
 * \param[out]      fragment: Decoded fragment metadata
 * \return          XGL_OK or a malformed or oversized fragment error
 */
xgl_error_t transport_decode_rx_fragment(const xgl_transport_ctx_t* ctx,
                                         const xgl_packet_t* packet,
                                         transport_rx_fragment_t* fragment) {
    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&cursor, packet->extensions,
                                               packet->extensions_len);
    if (err != XGL_OK) {
        return err;
    }

    bool found = false;
    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type != XGL_WIRE_EXT_FRAGMENT) {
            continue;
        }
        if (found) {
            return XGL_ERR_INVALID_FRAME;
        }
        err = xgl_wire_decode_fragment_ext_value(
            ext.value, ext.len, &fragment->message_id, &fragment->offset,
            &fragment->message_len);
        if (err != XGL_OK) {
            return err;
        }
        found = true;
    }
    if (err != XGL_ERR_NOT_FOUND) {
        return err;
    }
    if (!found || fragment->message_len == 0U ||
        fragment->offset > fragment->message_len ||
        packet->data->data_len == 0U ||
        packet->data->data_len > fragment->message_len - fragment->offset) {
        return XGL_ERR_INVALID_FRAME;
    }
    if (fragment->message_len > ctx->max_message_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    return XGL_OK;
}

/**
 * \brief           Match immutable attributes of all owned message fragments
 */
xgl_error_t transport_validate_rx_fragment_scope(
    const xgl_transport_ctx_t* ctx, const xgl_transport_peer_state_t* peer,
    const xgl_packet_t* packet, const transport_rx_fragment_t* fragment) {
    const bool reliable = packet->reliable == XGL_RELIABILITY_ACK_ELICITING;
    xgl_error_t error = xgl_fragment_validate_ext_scope(
        ctx->fragment_mgr, packet->source_id, packet->connection_id,
        packet->session_epoch, packet->data_type, fragment->message_id,
        fragment->message_len, reliable);
    if (error != XGL_OK) {
        return error;
    }
#if XGL_FEATURE_OUT_OF_ORDER
    if (peer != NULL) {
        for (const xgl_transport_rx_buffered_packet_t* buffered =
                 peer->rx_buffered;
             buffered != NULL; buffered = buffered->next) {
            if (!buffered->packet.fragment) {
                continue;
            }
            transport_rx_fragment_t owned_fragment;
            error = transport_decode_rx_fragment(ctx, &buffered->packet,
                                                 &owned_fragment);
            if (error != XGL_OK) {
                return error;
            }
            if (owned_fragment.message_id == fragment->message_id &&
                (buffered->packet.reliable != packet->reliable ||
                 buffered->packet.data_type != packet->data_type ||
                 owned_fragment.message_len != fragment->message_len)) {
                return XGL_ERR_INVALID_FRAME;
            }
        }
    }
#else
    (void)peer;
#endif
    return XGL_OK;
}

#if XGL_FEATURE_ADVANCED_RUNTIME
#if XGL_FEATURE_OUT_OF_ORDER
bool transport_expire_rx_buffered_fragments(xgl_transport_ctx_t* ctx,
                                            xgl_handle_t handle,
                                            xgl_transport_peer_state_t* peer) {
    if (peer == NULL || ctx->fragment_mgr == NULL) {
        return false;
    }
    const uint32_t timeout_ms = ctx->fragment_mgr->reassembly_timeout_ms;
    if (timeout_ms == 0U) {
        return false;
    }
    for (const xgl_transport_rx_buffered_packet_t* buffered = peer->rx_buffered;
         buffered != NULL; buffered = buffered->next) {
        if (buffered->packet.fragment &&
            transport_now(ctx) - buffered->received_at_ms >= timeout_ms) {
            /* SACK has transferred responsibility even before in-order drain.
             */
            transport_fail_peer(ctx, handle, peer, XGL_ERR_TIMEOUT);
#if XGL_FEATURE_STATISTICS
            if (ctx->stats != NULL) {
                ctx->stats->rx_dropped++;
            }
#endif
            return true;
        }
    }
    return false;
}

/**
 * \brief           Preserve the oldest admission of this cached message
 * \param[in]       ctx: Transport clock and fragment validation limits
 * \param[in]       peer: Owner of cached reliable fragments
 * \param[in]       fragment: Message entering active reassembly
 * \param[out]      first_ms: Earliest admission, or now when no fragment exists
 * \return          XGL_OK or an invalid owned fragment error
 */
static xgl_error_t transport_rx_fragment_first_time(
    const xgl_transport_ctx_t* ctx, const xgl_transport_peer_state_t* peer,
    const transport_rx_fragment_t* fragment, uint32_t* first_ms) {
    const uint32_t now_ms = transport_now(ctx);
    *first_ms = now_ms;
    for (const xgl_transport_rx_buffered_packet_t* buffered = peer->rx_buffered;
         buffered != NULL; buffered = buffered->next) {
        if (!buffered->packet.fragment) {
            continue;
        }
        transport_rx_fragment_t owned_fragment;
        xgl_error_t error = transport_decode_rx_fragment(ctx, &buffered->packet,
                                                         &owned_fragment);
        if (error != XGL_OK) {
            return error;
        }
        if (owned_fragment.message_id == fragment->message_id &&
            now_ms - buffered->received_at_ms > now_ms - *first_ms) {
            *first_ms = buffered->received_at_ms;
        }
    }
    return XGL_OK;
}
#endif

bool transport_expire_rx_fragment(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                                  xgl_transport_peer_state_t* peer,
                                  const xgl_packet_t* packet,
                                  const transport_rx_fragment_t* fragment) {
    if (!fragment_expire_message(ctx->fragment_mgr, packet->source_id,
                                 packet->connection_id, packet->session_epoch,
                                 fragment->message_id, transport_now(ctx))) {
        return false;
    }
    if (packet->reliable == XGL_RELIABILITY_ACK_ELICITING && peer != NULL) {
        transport_fail_peer(ctx, handle, peer, XGL_ERR_TIMEOUT);
    } else {
        xgl_transport_report_error(
            ctx, handle, XGL_ERR_TIMEOUT,
            XGL_ERROR_MESSAGE("Fragment reassembly timeout"));
    }
#if XGL_FEATURE_STATISTICS
    if (ctx->stats != NULL) {
        ctx->stats->rx_dropped++;
    }
#endif
    return true;
}
#endif

/**
 * \brief           Release a peer-owned completed message and its memory budget
 * \param[in,out]   ctx: Transport layer context
 * \param[in,out]   peer: Owning peer
 */
void transport_clear_pending_message(xgl_transport_ctx_t* ctx,
                                     xgl_transport_peer_state_t* peer) {
    if (peer == NULL || peer->rx_pending_message.data == NULL) {
        return;
    }
    xgl_fragment_release_message(ctx->fragment_mgr, &peer->rx_pending_message);
}

/**
 * \brief           Retry delivery while retaining bytes on temporary
 *                  backpressure
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Owning peer
 * \return          Application admission result
 */
xgl_error_t transport_drain_pending_message(xgl_transport_ctx_t* ctx,
                                            xgl_handle_t handle,
                                            xgl_transport_peer_state_t* peer) {
    if (peer->rx_pending_message.data == NULL) {
        return XGL_OK;
    }
    xgl_error_t err = transport_accept_payload(
#if XGL_FEATURE_ADVANCED_RUNTIME
        &peer->rx_pending_info,
#endif
        ctx, handle, peer->peer_id, peer->rx_pending_message_type,
        peer->rx_pending_message.data, peer->rx_pending_message.len);
    if (err == XGL_OK) {
        transport_clear_pending_message(ctx, peer);
    } else if (err != XGL_ERR_BUSY && err != XGL_ERR_NO_MEMORY) {
        transport_fail_peer(ctx, handle, peer, err);
    }
    return err;
}

/**
 * \brief           Retain admitted fragment bytes until completion or failure
 */
xgl_error_t
transport_deliver_rx_fragment(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                              xgl_transport_peer_state_t* peer,
                              const xgl_packet_t* packet, const uint8_t* data,
                              size_t data_len,
                              const transport_rx_fragment_t* fragment) {
    if (peer == NULL) {
        peer = transport_get_or_create_rx_peer(ctx, packet);
    }
    if (peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    if (peer->rx_pending_message.data != NULL) {
        return XGL_ERR_BUSY;
    }

#if XGL_FEATURE_ADVANCED_RUNTIME
    if (transport_expire_rx_fragment(ctx, handle, peer, packet, fragment)) {
        return XGL_ERR_TIMEOUT;
    }
#endif
    uint32_t first_ms = transport_now(ctx);
#if XGL_FEATURE_ADVANCED_RUNTIME && XGL_FEATURE_OUT_OF_ORDER
    xgl_error_t admission_error =
        transport_rx_fragment_first_time(ctx, peer, fragment, &first_ms);
    if (admission_error != XGL_OK) {
        return admission_error;
    }
#endif
    xgl_fragment_message_t complete = {0};
    xgl_error_t err = xgl_fragment_process_ext_scoped(
        ctx->fragment_mgr, packet->source_id, packet->connection_id,
        packet->session_epoch, packet->data_type, fragment->message_id,
        fragment->offset, fragment->message_len, data, data_len,
        packet->reliable == XGL_RELIABILITY_ACK_ELICITING, &complete, first_ms);
    if (err == XGL_ERR_BUSY) {
        return XGL_OK;
    }
    if (err != XGL_OK) {
        return err;
    }

#if XGL_FEATURE_ADVANCED_RUNTIME
    const xgl_rx_info_t info = {
        packet->source_id, packet->connection_id, packet->session_epoch,
        packet->data_type, packet->reliable == XGL_RELIABILITY_ACK_ELICITING};
#endif
    err = transport_accept_payload(
#if XGL_FEATURE_ADVANCED_RUNTIME
        &info,
#endif
        ctx, handle, packet->source_id, packet->data_type, complete.data,
        complete.len);
    if (err == XGL_ERR_BUSY || err == XGL_ERR_NO_MEMORY) {
        peer->rx_pending_message = complete;
        peer->rx_pending_message_type = packet->data_type;
#if XGL_FEATURE_ADVANCED_RUNTIME
        peer->rx_pending_info = info;
#endif
        /* Ownership was admitted; retry the application during maintenance. */
        return XGL_OK;
    }
    xgl_fragment_release_message(ctx->fragment_mgr, &complete);
    if (err != XGL_OK) {
        transport_fail_peer(ctx, handle, peer, err);
    }
    return err;
}

#endif

/**
 * \brief           Admit packet data into reassembly storage or the application
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Packet metadata
 * \param[in]       data: Borrowed payload bytes
 * \param[in]       data_len: Payload size
 * \return          XGL_OK only after transport or application admission
 */
xgl_error_t transport_deliver_packet(xgl_transport_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_packet_t* packet,
                                     const uint8_t* data, size_t data_len) {
    if (ctx == NULL || packet == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint16_t source_id = packet->source_id;
    uint8_t data_type = packet->data_type;
#if XGL_FEATURE_FRAGMENTATION
    xgl_transport_peer_state_t* peer = transport_find_rx_peer(ctx, packet);
    if (peer != NULL && peer->rx_pending_message.data != NULL) {
        return XGL_ERR_BUSY;
    }

    if (packet->fragment && ctx->fragment_mgr != NULL) {
        transport_rx_fragment_t fragment;
        xgl_error_t err = transport_decode_rx_fragment(ctx, packet, &fragment);
        if (err != XGL_OK) {
            return err;
        }
        return transport_deliver_rx_fragment(ctx, handle, peer, packet, data,
                                             data_len, &fragment);
    }
#endif

#if XGL_FEATURE_ADVANCED_RUNTIME
    const xgl_rx_info_t info = {
        source_id, packet->connection_id, packet->session_epoch, data_type,
        packet->reliable == XGL_RELIABILITY_ACK_ELICITING};
#endif
    return transport_accept_payload(
#if XGL_FEATURE_ADVANCED_RUNTIME
        &info,
#endif
        ctx, handle, source_id, data_type, data, data_len);
}
