/**
 * \file            xgl_transport_ack.c
 * \brief           Transport ACK/SACK processing
 */

#include "xgl_transport_internal.h"
#include "xgl/internal/xgl_time.h"
#include "xgl/internal/xgl_wire.h"
static void transport_mark_ack_range_windows(xgl_transport_ctx_t* ctx,
                                             xgl_transport_peer_state_t* peer,
                                             uint32_t largest_ack,
                                             const xgl_wire_ack_range_t* ranges,
                                             size_t range_count) {
    uint64_t next_high = largest_ack;

    for (size_t i = 0; i < range_count; ++i) {
        if (ranges[i].length == 0U) {
            continue;
        }

        uint64_t high = next_high;
        if (i > 0U) {
            uint64_t skip = (uint64_t)ranges[i].gap + 1U;
            if (high < skip) {
                break;
            }
            high -= skip;
        }

        uint64_t low = 0U;
        if (high + 1U > ranges[i].length) {
            low = high - (uint64_t)ranges[i].length + 1U;
        }

        for (uint64_t packet_number = high;; --packet_number) {
            (void)xgl_window_mark_ack_packet_number(&peer->tx_window,
                                                    (uint32_t)packet_number);
            if (xgl_window_is_in_window_packet_number(&ctx->window,
                                                      (uint32_t)packet_number)) {
                (void)xgl_window_mark_ack_packet_number(&ctx->window,
                                                        (uint32_t)packet_number);
            }

            if (packet_number == low) {
                break;
            }
        }

        next_high = low;
    }

    (void)xgl_window_advance_base_packet_number(&peer->tx_window);
    (void)xgl_window_advance_base_packet_number(&ctx->window);
}

xgl_error_t transport_try_process_ack_range_ext(xgl_transport_ctx_t* ctx,
                                                       xgl_transport_peer_state_t* peer,
                                                       uint16_t source_id,
                                                       const uint8_t* data,
                                                       size_t data_len,
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
        if (ext.type != XGL_WIRE_EXT_ACK_RANGE) {
            continue;
        }

        uint32_t largest_ack = 0;
        uint32_t ack_delay_us = 0;
        xgl_wire_ack_range_t ranges[64];
        size_t range_count = 0;
        err = xgl_wire_decode_ack_range_ext_value(ext.value,
                                                  ext.len,
                                                  &largest_ack,
                                                  &ack_delay_us,
                                                  ranges,
                                                  64U,
                                                  &range_count);
        (void)ack_delay_us;
        if (err != XGL_OK) {
            return err;
        }

        size_t removed = xgl_reliable_remove_ack_ranges(&peer->reliable_queue,
                                                        source_id,
                                                        largest_ack,
                                                        ranges,
                                                        range_count);
        if (removed == 0U) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        transport_mark_ack_range_windows(ctx, peer, largest_ack, ranges, range_count);
        *handled = true;
        return XGL_OK;
    }

    if (err == XGL_ERR_NOT_FOUND) {
        return XGL_OK;
    }

    return err;
}

static bool transport_sack_bit_is_set(const uint8_t* bitmap, size_t bit_index) {
    return (bitmap[bit_index / 8U] & (uint8_t)(1U << (bit_index % 8U))) != 0U;
}

static xgl_error_t transport_process_sack_value(xgl_transport_ctx_t* ctx,
                                                xgl_handle_t handle,
                                                xgl_transport_peer_state_t* peer,
                                                uint16_t source_id,
                                                const uint8_t* value,
                                                size_t value_len,
                                                size_t* retransmitted) {
    if (ctx == NULL || peer == NULL || value == NULL || retransmitted == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint32_t base_packet = 0U;
    uint8_t bitmap[64] = {0};
    size_t bitmap_len = 0U;
    xgl_error_t err = xgl_wire_decode_sack_ext_value(value,
                                                     value_len,
                                                     &base_packet,
                                                     bitmap,
                                                     sizeof(bitmap),
                                                     &bitmap_len);
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
    if (!has_set) {
        xgl_reliable_packet_t* missing =
            xgl_reliable_find_packet_number(&peer->reliable_queue,
                                            base_packet,
                                            source_id);
        if (missing == NULL) {
            return XGL_ERR_SEQUENCE_ERROR;
        }

        err = transport_retransmit_reliable_packet(ctx,
                                                   handle,
                                                   missing,
                                                   xgl_time_ms());
        if (err != XGL_OK) {
            return err;
        }
        (*retransmitted)++;
        return XGL_OK;
    }

    uint32_t now = xgl_time_ms();
    for (size_t i = 0; i <= highest_set; ++i) {
        uint32_t packet_number = base_packet + (uint32_t)i;
        bool received = transport_sack_bit_is_set(bitmap, i);
        if (received) {
            const xgl_reliable_packet_t* rel_packet =
                xgl_reliable_find_packet_number(&peer->reliable_queue,
                                                packet_number,
                                                source_id);
            if (rel_packet != NULL) {
                (void)xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                                        packet_number,
                                                        source_id);
                (void)xgl_window_mark_ack_packet_number(&peer->tx_window,
                                                        packet_number);
                if (xgl_window_is_in_window_packet_number(&ctx->window,
                                                          packet_number)) {
                    (void)xgl_window_mark_ack_packet_number(&ctx->window,
                                                            packet_number);
                }
            }
            continue;
        }

        xgl_reliable_packet_t* missing =
            xgl_reliable_find_packet_number(&peer->reliable_queue,
                                            packet_number,
                                            source_id);
        if (missing != NULL) {
            err = transport_retransmit_reliable_packet(ctx, handle, missing, now);
            if (err != XGL_OK) {
                return err;
            }
            (*retransmitted)++;
        }
    }

    (void)xgl_window_advance_base_packet_number(&peer->tx_window);
    (void)xgl_window_advance_base_packet_number(&ctx->window);

    return XGL_OK;
}

xgl_error_t transport_try_process_sack_ext(xgl_transport_ctx_t* ctx,
                                                  xgl_handle_t handle,
                                                  xgl_transport_peer_state_t* peer,
                                                  uint16_t source_id,
                                                  const uint8_t* data,
                                                  size_t data_len,
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
        err = transport_process_sack_value(ctx,
                                           handle,
                                           peer,
                                           source_id,
                                           ext.value,
                                           ext.len,
                                           &retransmitted);
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

/*---------------------------------------------------------------------------*/
