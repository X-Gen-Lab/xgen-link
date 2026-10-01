/**
 * \file            xgl_transport_ack.c
 * \brief           Acknowledgement validation and reliable completion
 */

#include "wire/xgl_wire.h"
#include "xgl_transport_internal.h"
#include <limits.h>

typedef struct {
    xgl_wire_ack_range_view_t view;
    size_t index;
    uint32_t next_high;
} ack_ranges_t;

/**
 * \brief           Initialize a bounded cursor over an ACK range extension
 * \param[out]      cursor: Range cursor
 * \param[in]       ext: Fully contained ACK range extension
 * \return          XGL_OK if the extension length and count are valid
 */
static xgl_error_t ack_ranges_init(ack_ranges_t* cursor,
                                   const xgl_wire_ext_t* ext) {
    xgl_error_t err =
        xgl_wire_decode_ack_range_view(ext->value, ext->len, &cursor->view);
    if (err != XGL_OK || cursor->view.range_count == 0U) {
        return err != XGL_OK ? err : XGL_ERR_INVALID_FRAME;
    }
    cursor->index = 0U;
    cursor->next_high = cursor->view.largest_ack;
    return XGL_OK;
}

/**
 * \brief           Read the next inclusive ACK range without numeric wraparound
 * \param[in,out]   cursor: Range cursor
 * \param[out]      low: Lowest acknowledged packet number
 * \param[out]      high: Highest acknowledged packet number
 * \return          XGL_OK, XGL_ERR_NOT_FOUND at end, or a validation error
 */
static xgl_error_t ack_ranges_next(ack_ranges_t* cursor, uint32_t* low,
                                   uint32_t* high) {
    if (cursor->index == cursor->view.range_count) {
        return XGL_ERR_NOT_FOUND;
    }
    xgl_wire_ack_range_t range;
    xgl_error_t err =
        xgl_wire_ack_range_at(&cursor->view, cursor->index, &range);
    if (err != XGL_OK) {
        return err;
    }
    uint32_t gap = range.gap;
    uint32_t length = range.length;
    *high = cursor->next_high;
    if (length == 0U || (cursor->index == 0U && gap != 0U)) {
        return XGL_ERR_INVALID_FRAME;
    }
    if (cursor->index != 0U) {
        if (*high < gap + 1U) {
            return XGL_ERR_INVALID_FRAME;
        }
        *high -= gap + 1U;
    }
    if ((uint64_t)length > (uint64_t)*high + 1U) {
        return XGL_ERR_INVALID_FRAME;
    }
    *low = *high - (length - 1U);
    cursor->next_high = *low;
    cursor->index++;
    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Complete one actually transmitted packet in its owning peer
 * \param[in,out]   ctx: Transport layer context
 * \param[in,out]   peer: Owning peer
 * \param[in]       packet_number: Acknowledged packet number
 * \param[in]       now_ms: Current transport clock
 */
static void transport_acknowledge_packet(xgl_transport_ctx_t* ctx,
                                         xgl_transport_peer_state_t* peer,
                                         uint32_t packet_number,
                                         uint32_t now_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
#if !XGL_FEATURE_STATISTICS
    (void)ctx;
#endif
    const xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(
        &peer->reliable_queue, packet_number, peer->peer_id);
    if (packet == NULL || !packet->sent) {
        return;
    }
    uint32_t elapsed = now_ms - packet->send_timestamp;
    if (packet->retry_count == 0U && elapsed > 0U && elapsed <= INT32_MAX) {
        xgl_rtt_update(&peer->rtt_est, (int32_t)elapsed);
#if XGL_FEATURE_STATISTICS
        if (ctx->rtt_sample_count < UINT32_MAX) {
            ctx->rtt_total_ms += elapsed;
            ctx->rtt_sample_count++;
            if (elapsed < ctx->rtt_min_ms) {
                ctx->rtt_min_ms = elapsed;
            }
            if (elapsed > ctx->rtt_max_ms) {
                ctx->rtt_max_ms = elapsed;
            }
        }
#endif
    }
    (void)xgl_window_mark_ack_packet_number(&peer->tx_window, packet_number);
    (void)xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                            packet_number, peer->peer_id);
}

/**
 * \brief           Validate all ACK ranges before mutating the owning peer
 * \param[in,out]   ctx: Transport context
 * \param[in,out]   peer: Exact owner selected by the validated packet scope
 * \param[in]       ext: Borrowed, structurally validated ACK range extension
 * \return          XGL_OK or a range/sequence validation error
 */
static xgl_error_t transport_process_ack_range(xgl_transport_ctx_t* ctx,
                                               xgl_transport_peer_state_t* peer,
                                               const xgl_wire_ext_t* ext) {
    ack_ranges_t ranges;
    xgl_error_t err = ack_ranges_init(&ranges, ext);
    if (err != XGL_OK) {
        return err;
    }
    uint32_t low, high;
    while ((err = ack_ranges_next(&ranges, &low, &high)) == XGL_OK) {
        if (high >= peer->tx_window.next_packet_number) {
            return XGL_ERR_SEQUENCE_ERROR;
        }
    }
    if (err != XGL_ERR_NOT_FOUND) {
        return err;
    }

    /* Visit actual outstanding records, never every number in an ACK span. */
    uint32_t now = transport_now(ctx);
    xgct_list_node_t *node, *next;
    XGCT_LIST_FOR_EACH_SAFE(&peer->reliable_queue.wait_ack_list, node, next) {
        const xgl_reliable_packet_t* packet =
            /* The queue owns this intrusive record. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        ranges.index = 0U;
        ranges.next_high = ranges.view.largest_ack;
        while (ack_ranges_next(&ranges, &low, &high) == XGL_OK) {
            if (packet->packet_number >= low && packet->packet_number <= high) {
                transport_acknowledge_packet(ctx, peer, packet->packet_number,
                                             now);
                break;
            }
        }
    }
    (void)xgl_window_advance_base_packet_number(&peer->tx_window);
    peer->last_active_ms = now;
    return XGL_OK;
}

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
    /* Commit the complete acknowledgement before retrying any missing packet.
     * Local send backpressure cannot invalidate already received peer data. */
    for (size_t i = 0; i <= highest_set; ++i) {
        if (transport_sack_bit_is_set(bitmap, i)) {
            transport_acknowledge_packet(ctx, peer, base_packet + (uint32_t)i,
                                         now);
        }
    }
    (void)xgl_window_advance_base_packet_number(&peer->tx_window);

    for (size_t i = 0; i <= highest_set; ++i) {
        if (transport_sack_bit_is_set(bitmap, i)) {
            continue;
        }
        uint32_t packet_number = base_packet + (uint32_t)i;
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

    return XGL_OK;
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

    xgl_transport_peer_state_t* peer = transport_find_rx_peer(ctx, packet);
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
    xgl_wire_ext_t acknowledgement = {0};
    bool found_ack = false;
    while ((validation = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type == XGL_WIRE_EXT_ACK_RANGE ||
            ext.type == XGL_WIRE_EXT_SACK) {
            if (found_ack) {
                return XGL_ERR_INVALID_FRAME;
            }
            found_ack = true;
            acknowledgement = ext;
        }
    }
    if (validation != XGL_ERR_NOT_FOUND) {
        return validation;
    }
    if (!found_ack) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_error_t err;
    if (acknowledgement.type == XGL_WIRE_EXT_ACK_RANGE) {
        err = transport_process_ack_range(ctx, peer, &acknowledgement);
    } else {
        size_t retransmitted = 0U;
        err = transport_process_sack_value(ctx, handle, peer, packet->source_id,
                                           acknowledgement.value,
                                           acknowledgement.len, &retransmitted);
#if XGL_FEATURE_STATISTICS
        if (err == XGL_OK && retransmitted > 0U && ctx->tx_retries != NULL) {
            *ctx->tx_retries += retransmitted;
        }
#endif
    }
    if (err != XGL_OK) {
        return err;
    }
#if XGL_FEATURE_FRAGMENTATION
    (void)transport_pump_tx_message(ctx, handle, peer);
#endif
    return XGL_OK;
}
