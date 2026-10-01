/**
 * \file            xgl_transport_rx_delivery.c
 * \brief           Application admission and completed-message ownership
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

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
static xgl_error_t transport_accept_payload(xgl_transport_ctx_t* ctx,
                                            xgl_handle_t handle,
                                            uint16_t source_id,
                                            uint8_t data_type,
                                            const uint8_t* data, size_t len) {
    if (ctx->rx_accept_callback != NULL) {
        xgl_error_t err = ctx->rx_accept_callback(
            handle, source_id, data_type, data, len, ctx->callback_user_data);
        if (err != XGL_OK) {
            return err;
        }
    } else if (ctx->rx_callback != NULL) {
        ctx->rx_callback(handle, source_id, data_type, data, len,
                         ctx->callback_user_data);
    }
    if ((XGL_FEATURE_STATISTICS && ctx->stats != NULL)) {
        ctx->stats->rx_packets++;
        ctx->stats->rx_bytes += len;
    }
    return XGL_OK;
}

#if XGL_FEATURE_FRAGMENTATION
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
        ctx, handle, peer->peer_id, peer->rx_pending_message_type,
        peer->rx_pending_message.data, peer->rx_pending_message.len);
    if (err == XGL_OK) {
        transport_clear_pending_message(ctx, peer);
    } else if (err != XGL_ERR_BUSY && err != XGL_ERR_NO_MEMORY) {
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
        if (peer == NULL) {
            peer = transport_get_or_create_rx_peer(ctx, packet);
        }
        if (peer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
        xgl_fragment_message_t complete = {0};
        uint32_t message_id = 0U;
        uint32_t fragment_offset = 0U;
        uint32_t message_len = 0U;
        bool has_fragment_ext = false;

        if (packet->extensions != NULL && packet->extensions_len > 0U) {
            xgl_wire_ext_cursor_t cursor;
            xgl_error_t err = xgl_wire_ext_cursor_init(
                &cursor, packet->extensions, packet->extensions_len);
            if (err != XGL_OK) {
                return err;
            }

            xgl_wire_ext_t ext;
            while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
                if (ext.type == XGL_WIRE_EXT_FRAGMENT) {
                    err = xgl_wire_decode_fragment_ext_value(
                        ext.value, ext.len, &message_id, &fragment_offset,
                        &message_len);
                    if (err != XGL_OK) {
                        return err;
                    }
                    has_fragment_ext = true;
                    break;
                }
            }
            if (err != XGL_OK && err != XGL_ERR_NOT_FOUND) {
                return err;
            }
        }

        if (!has_fragment_ext) {
            return XGL_ERR_INVALID_FRAME;
        }

        xgl_error_t err = xgl_fragment_process_ext(
            ctx->fragment_mgr, source_id, packet->connection_id,
            packet->session_epoch, data_type, message_id, fragment_offset,
            message_len, data, data_len, &complete, transport_now(ctx));

        if (err == XGL_OK) {
            err = transport_accept_payload(ctx, handle, source_id, data_type,
                                           complete.data, complete.len);
            if (err == XGL_ERR_BUSY || err == XGL_ERR_NO_MEMORY) {
                peer->rx_pending_message = complete;
                peer->rx_pending_message_type = data_type;
                return XGL_OK; /* Accepted into owned storage, retry application
                                  in run. */
            }
            xgl_fragment_release_message(ctx->fragment_mgr, &complete);
            if (err != XGL_OK) {
                transport_fail_peer(ctx, handle, peer, err);
                return err;
            }
        } else if (err == XGL_ERR_BUSY) {
            return XGL_OK;
        } else {
            return err;
        }
        return XGL_OK;
    }
#endif

    return transport_accept_payload(ctx, handle, source_id, data_type, data,
                                    data_len);
}
