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
    if (length - 1U > *high) {
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
 * \param[in]       packet: Live record already found in the owning queue
 * \param[in]       now_ms: Current transport clock
 */
static void transport_acknowledge_packet(xgl_transport_ctx_t* ctx,
                                         xgl_transport_peer_state_t* peer,
                                         xgl_reliable_packet_t* packet,
                                         uint32_t now_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
#if !XGL_FEATURE_STATISTICS
    (void)ctx;
#endif
    if (packet == NULL || !packet->sent || packet->target_id != peer->peer_id) {
        return;
    }
#if XGL_FEATURE_ADAPTIVE_RTO
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
#else
    (void)now_ms;
#endif
    (void)xgl_window_mark_ack_packet_number(&peer->tx_window,
                                            packet->packet_number);
    (void)xgl_reliable_remove_owned(&peer->reliable_queue, packet);
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

    /* Peer admission appends strictly increasing packet numbers. Walk that
     * queue backwards with the descending ranges, without rescanning either.
     * Complete validation above remains separate from all mutations. */
    uint32_t now = transport_now(ctx);
    ranges.index = 0U;
    ranges.next_high = ranges.view.largest_ack;
    bool has_range = ack_ranges_next(&ranges, &low, &high) == XGL_OK;
    xgct_list_node_t* node = peer->reliable_queue.wait_ack_list.tail;
    while (node != NULL && has_range) {
        xgct_list_node_t* previous = node->prev;
        xgl_reliable_packet_t* packet =
            /* The queue owns this intrusive record. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        while (has_range && packet->packet_number < low) {
            has_range = ack_ranges_next(&ranges, &low, &high) == XGL_OK;
        }
        if (has_range && packet->packet_number <= high) {
            transport_acknowledge_packet(ctx, peer, packet, now);
        }
        node = previous;
    }
    (void)xgl_window_advance_base_packet_number(&peer->tx_window);
    peer->last_active_ms = now;
    return XGL_OK;
}

static bool transport_sack_bit_is_set(const uint8_t* bitmap, size_t bit_index) {
    return (bitmap[bit_index / 8U] & (uint8_t)(1U << (bit_index % 8U))) != 0U;
}

/**
 * \brief           Schedule a missing original packet before RTO recovery
 * \param[in,out]   packet: Outstanding packet, or NULL if already acknowledged
 * \note            Maintenance submits the retry after the receive stack has
 *                  unwound. Duplicate feedback preserves a local retry delay.
 */
static void transport_schedule_fast_retry(xgl_reliable_packet_t* packet) {
    if (packet != NULL && packet->sent && packet->retry_count == 0U) {
        packet->fast_retry_pending = true;
    }
}

static xgl_error_t transport_process_sack_value(
    xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    uint16_t source_id, const uint8_t* value, size_t value_len) {
    if (ctx == NULL || peer == NULL || value == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_wire_sack_view_t view;
    xgl_error_t err = xgl_wire_decode_sack_view(value, value_len, &view);
    if (err != XGL_OK) {
        return err;
    }
    /* Preserve the receive bound formerly imposed by the copied bitmap. */
    if (view.bitmap_len > 64U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (view.bitmap_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t remaining = view.bitmap_len;
    while (remaining > 0U && view.bitmap[remaining - 1U] == 0U) {
        --remaining;
    }
    const bool has_set = remaining != 0U;
    size_t highest_set = 0U;
    if (has_set) {
        highest_set = (remaining - 1U) * 8U;
        uint8_t byte = view.bitmap[remaining - 1U];
        while (byte > 1U) {
            byte >>= 1U;
            highest_set++;
        }
    }
    /* Validate the complete claimed range before acknowledgements or retries.
     */
    if (view.base_packet >= peer->tx_window.next_packet_number ||
        (has_set && highest_set >= peer->tx_window.next_packet_number -
                                       view.base_packet)) {
        return XGL_ERR_SEQUENCE_ERROR;
    }
    if (!has_set) {
        xgl_reliable_packet_t* missing = xgl_reliable_find_packet_number(
            &peer->reliable_queue, view.base_packet, source_id);
        transport_schedule_fast_retry(missing);
        return XGL_OK;
    }

    uint32_t now = transport_now(ctx);
    /* Visit actual records rather than every bitmap position. Commit all
     * acknowledgements before marking any missing record for later retry. */
    xgct_list_node_t *node, *next;
    XGCT_LIST_FOR_EACH_SAFE(&peer->reliable_queue.wait_ack_list, node, next) {
        /* The queue owns this intrusive record. */
        xgl_reliable_packet_t* packet =
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        if (packet->packet_number >= view.base_packet) {
            size_t bit = packet->packet_number - view.base_packet;
            if (bit <= highest_set &&
                transport_sack_bit_is_set(view.bitmap, bit)) {
                transport_acknowledge_packet(ctx, peer, packet, now);
            }
        }
    }
    (void)xgl_window_advance_base_packet_number(&peer->tx_window);

    XGCT_LIST_FOR_EACH(&peer->reliable_queue.wait_ack_list, node) {
        /* The queue owns this intrusive record. */
        xgl_reliable_packet_t* packet =
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        if (packet->target_id == source_id &&
            packet->packet_number >= view.base_packet &&
            packet->packet_number - view.base_packet <= highest_set) {
            transport_schedule_fast_retry(packet);
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
    (void)handle;
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

#if XGL_FEATURE_ADVANCED_RUNTIME
    if (transport_expire_tx(ctx, handle, peer)) {
        return XGL_ERR_TIMEOUT;
    }
#endif
    xgl_error_t err;
    if (acknowledgement.type == XGL_WIRE_EXT_ACK_RANGE) {
        err = transport_process_ack_range(ctx, peer, &acknowledgement);
    } else {
        err = transport_process_sack_value(ctx, peer, packet->source_id,
                                           acknowledgement.value,
                                           acknowledgement.len);
    }
    if (err != XGL_OK) {
        return err;
    }
#if XGL_FEATURE_ADVANCED_RUNTIME
    transport_notify_tx_drained(ctx, handle, peer);
#endif
    return XGL_OK;
}
