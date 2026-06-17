/**
 * \file            xgl_transport_ack.c
 * \brief           Transport ACK range processing
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

/**
 * \brief           Send ACK packet for received data
 * \param[in]       ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       packet_number: Packet number to acknowledge
 * \param[in]       source_id: Source node ID
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t transport_send_ack(const xgl_transport_ctx_t *ctx,
                               xgl_handle_t handle, uint32_t packet_number,
                               uint16_t source_id, uint16_t session_id,
                               uint32_t connection_id, uint32_t session_epoch)
{
    uint8_t ack_value[16] = {0};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {{.gap = 0, .length = 1}};

    xgl_error_t err = xgl_wire_encode_ack_range_ext_value(
        ack_value, sizeof(ack_value), packet_number, 0, ranges, 1,
        &ack_value_len);
    if (err != XGL_OK) {
        return err;
    }

    uint8_t ack_ext[32] = {0};
    size_t ack_ext_len = 0;
    err = xgl_wire_encode_ext(ack_ext, sizeof(ack_ext), XGL_WIRE_EXT_ACK_RANGE,
                              ack_value, ack_value_len, &ack_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_packet_data_t ack_packet_data = {
        .ref_count = 1, .data_len = 0, .data = NULL, .owned_data = NULL};

    xgl_packet_t ack_packet = {.source_id = ctx->local_id,
                               .target_id = source_id,
                               .session_id = session_id,
                               .connection_id = connection_id,
                               .packet_number = packet_number,
                               .session_epoch = session_epoch,
                               .packet_type = XGL_PACKET_TYPE_ACK,
                               .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
                               .data_type = 0,
                               .reliable = XGL_RELIABILITY_ACK_ONLY,
                               .fragment = false,
                               .priority = 7,
                               .data = &ack_packet_data,
                               .extensions = ack_ext,
                               .extensions_len = ack_ext_len,
                               .phy = NULL};

    if (ctx->lower_layer != NULL && ctx->lower_layer->send != NULL) {
        return xgl_layer_send(ctx->lower_layer, handle, &ack_packet);
    }

    return XGL_ERR_INVALID_PARAM;
}

static void transport_mark_ack_range_windows(xgl_transport_ctx_t *ctx,
                                             xgl_transport_peer_state_t *peer,
                                             uint32_t largest_ack,
                                             const xgl_wire_ack_range_t *ranges,
                                             size_t range_count)
{
    uint64_t next_high = largest_ack;

    for (size_t i = 0; i < range_count; ++i) {
        if (ranges[i].length == 0U) {
            continue;
        }

        uint64_t high = next_high;
        if (i > 0U) {
            uint64_t skip = (uint64_t) ranges[i].gap + 1U;
            if (high < skip) {
                break;
            }
            high -= skip;
        }

        uint64_t low = 0U;
        if (high + 1U > ranges[i].length) {
            low = high - (uint64_t) ranges[i].length + 1U;
        }

        for (uint64_t packet_number = high;; --packet_number) {
            (void) xgl_window_mark_ack_packet_number(&peer->tx_window,
                                                     (uint32_t) packet_number);
            if (xgl_window_is_in_window_packet_number(
                    &ctx->window, (uint32_t) packet_number)) {
                (void) xgl_window_mark_ack_packet_number(
                    &ctx->window, (uint32_t) packet_number);
            }

            if (packet_number == low) {
                break;
            }
        }

        next_high = low;
    }

    (void) xgl_window_advance_base_packet_number(&peer->tx_window);
    (void) xgl_window_advance_base_packet_number(&ctx->window);
}

xgl_error_t transport_try_process_ack_range_ext(
    xgl_transport_ctx_t *ctx, xgl_transport_peer_state_t *peer,
    uint16_t source_id, const uint8_t *data, size_t data_len, bool *handled)
{
    if (ctx == NULL || peer == NULL || handled == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *handled = false;
    if (data == NULL || data_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&cursor, data, data_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type != XGL_WIRE_EXT_ACK_RANGE) {
            continue;
        }

        uint32_t largest_ack = 0;
        uint32_t ack_delay_us = 0;
        xgl_wire_ack_range_t ranges[64];
        size_t range_count = 0;
        err = xgl_wire_decode_ack_range_ext_value(ext.value, ext.len,
                                                  &largest_ack, &ack_delay_us,
                                                  ranges, 64U, &range_count);
        (void) ack_delay_us;
        if (err != XGL_OK) {
            return err;
        }

        size_t removed = xgl_reliable_remove_ack_ranges(
            &peer->reliable_queue, source_id, largest_ack, ranges, range_count);
        if (removed == 0U) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        transport_mark_ack_range_windows(ctx, peer, largest_ack, ranges,
                                         range_count);
        *handled = true;
        return XGL_OK;
    }

    if (err == XGL_ERR_NOT_FOUND) {
        return XGL_OK;
    }

    return err;
}
