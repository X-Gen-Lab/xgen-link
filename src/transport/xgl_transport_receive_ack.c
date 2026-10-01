/**
 * \file            xgl_transport_receive_ack.c
 * \brief           Transport ACK receive dispatch
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

/**
 * \brief           Resolve an ACK using the exact production connection scope
 * \param[in]       ctx: Transport layer context
 * \param[in]       packet: Acknowledgement metadata
 * \return          Matching peer, or NULL without fallback to another scope
 */
static xgl_transport_peer_state_t*
transport_find_ack_peer(xgl_transport_ctx_t* ctx, const xgl_packet_t* packet) {
    return transport_find_peer_scope(
        ctx, packet->source_id, packet->connection_id, packet->session_epoch);
}

/**
 * \brief           Validate an entire acknowledgement before applying
 *                  completion
 * \param[in,out]   ctx: Transport layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet: Borrowed acknowledgement view
 * \return          XGL_OK or an acknowledgement validation error
 */
xgl_error_t transport_process_ack_packet(xgl_transport_ctx_t* ctx,
                                         xgl_handle_t handle,
                                         const xgl_packet_t* packet) {
    if (packet->packet_type != XGL_PACKET_TYPE_ACK) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_transport_peer_state_t* peer = transport_find_ack_peer(ctx, packet);
    if (peer == NULL) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    if (peer->failed) {
        return XGL_ERR_ACK_TIMEOUT;
    }

    if ((packet->flags & XGL_WIRE_FLAG_HAS_EXTENSIONS) == 0U ||
        packet->extensions == NULL || packet->extensions_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    /* Validate every TLV before any ACK can mutate peer state. */
    xgl_wire_ext_cursor_t cursor;
    xgl_error_t validation = xgl_wire_ext_cursor_init(
        &cursor, packet->extensions, packet->extensions_len);
    if (validation != XGL_OK) {
        return validation;
    }
    xgl_wire_ext_t ext;
    bool found_ack = false;
    while ((validation = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type == XGL_WIRE_EXT_ACK_RANGE ||
            ext.type == XGL_WIRE_EXT_SACK) {
            if (found_ack) {
                return XGL_ERR_INVALID_FRAME;
            }
            found_ack = true;
        }
    }
    if (validation != XGL_ERR_NOT_FOUND) {
        return validation;
    }
    if (!found_ack) {
        return XGL_ERR_INVALID_FRAME;
    }

    bool handled_ack_range = false;
    xgl_error_t err = transport_try_process_ack_range_ext(
        ctx, peer, packet->source_id, packet->extensions,
        packet->extensions_len, &handled_ack_range);
    if (err != XGL_OK) {
        return err;
    }
    if (handled_ack_range) {
#if XGL_FEATURE_FRAGMENTATION
        (void)transport_pump_tx_message(ctx, handle, peer);
#endif
        return XGL_OK;
    }

    bool handled_sack = false;
    err = transport_try_process_sack_ext(ctx, handle, peer, packet->source_id,
                                         packet->extensions,
                                         packet->extensions_len, &handled_sack);
    if (err != XGL_OK) {
        return err;
    }
    if (handled_sack) {
#if XGL_FEATURE_FRAGMENTATION
        (void)transport_pump_tx_message(ctx, handle, peer);
#endif
        return XGL_OK;
    }

    return XGL_ERR_INVALID_FRAME;
}
