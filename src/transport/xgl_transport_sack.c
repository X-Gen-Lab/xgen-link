/**
 * \file            xgl_transport_sack.c
 * \brief           Transport SACK processing
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

static bool transport_sack_bit_is_set(const uint8_t* bitmap, size_t bit_index) {
    return (bitmap[bit_index / 8U] & (uint8_t)(1U << (bit_index % 8U))) != 0U;
}

static xgl_error_t
transport_process_sack_value(xgl_transport_ctx_t* ctx, xgl_handle_t handle,
                             xgl_transport_peer_state_t* peer,
                             uint16_t source_id, const uint8_t* value,
                             size_t value_len, size_t* retransmitted) {
    if (ctx == NULL || peer == NULL || value == NULL || retransmitted == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint32_t base_packet = 0U;
    uint8_t bitmap[64] = {0};
    size_t bitmap_len = 0U;
    xgl_error_t err = xgl_wire_decode_sack_ext_value(
        value, value_len, &base_packet, bitmap, sizeof(bitmap), &bitmap_len);
    if (err != XGL_OK) {
        return err;
    }
    if (bitmap_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t highest_set = 0U;
    bool has_set = false;
    for (size_t i = 0; i < bitmap_len * 8U; ++i) {
        if (transport_sack_bit_is_set(bitmap, i)) {
            highest_set = i;
            has_set = true;
        }
    }
    /* Validate the complete claimed range before acknowledgements or retries.
     */
    if ((uint64_t)base_packet + (has_set ? highest_set : 0U) >=
        peer->tx_window.next_packet_number) {
        return XGL_ERR_SEQUENCE_ERROR;
    }
    if (!has_set) {
        xgl_reliable_packet_t* missing = xgl_reliable_find_packet_number(
            &peer->reliable_queue, base_packet, source_id);
        if (missing == NULL) {
            return XGL_OK;
        }

        err = transport_retransmit_reliable_packet(ctx, handle, peer, missing,
                                                   transport_now(ctx));
        if (err != XGL_OK) {
            return err;
        }
        (*retransmitted)++;
        return XGL_OK;
    }

    uint32_t now = transport_now(ctx);
    for (size_t i = 0; i <= highest_set; ++i) {
        uint32_t packet_number = base_packet + (uint32_t)i;
        bool received = transport_sack_bit_is_set(bitmap, i);
        if (received) {
            transport_acknowledge_packet(ctx, peer, packet_number, now);
            continue;
        }

        xgl_reliable_packet_t* missing = xgl_reliable_find_packet_number(
            &peer->reliable_queue, packet_number, source_id);
        if (missing != NULL) {
            err = transport_retransmit_reliable_packet(ctx, handle, peer,
                                                       missing, now);
            if (err != XGL_OK) {
                return err;
            }
            (*retransmitted)++;
        }
    }

    (void)xgl_window_advance_base_packet_number(&peer->tx_window);

    return XGL_OK;
}

xgl_error_t transport_try_process_sack_ext(xgl_transport_ctx_t* ctx,
                                           xgl_handle_t handle,
                                           xgl_transport_peer_state_t* peer,
                                           uint16_t source_id,
                                           const uint8_t* data, size_t data_len,
                                           bool* handled) {
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
        if (ext.type != XGL_WIRE_EXT_SACK) {
            continue;
        }

        size_t retransmitted = 0U;
        err = transport_process_sack_value(ctx, handle, peer, source_id,
                                           ext.value, ext.len, &retransmitted);
        if (err != XGL_OK) {
            return err;
        }

        if (retransmitted > 0U && ctx->tx_retries != NULL) {
            *ctx->tx_retries += retransmitted;
        }

        *handled = true;
        return XGL_OK;
    }

    if (err == XGL_ERR_NOT_FOUND) {
        return XGL_OK;
    }

    return err;
}
