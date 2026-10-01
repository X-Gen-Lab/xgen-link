/**
 * \file            xgl_transport_ack.c
 * \brief           Reliable ACK range validation and peer-owned completion
 */
#include <limits.h>

#include "xgen/bytes/bytes.h"
#include "xgl_transport_internal.h"

typedef struct {
    const uint8_t* value;
    size_t count;
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
    if (ext->len < 9U || ext->value == NULL) {
        return XGL_ERR_INVALID_FRAME;
    }
    size_t count = ext->value[8];
    if (count == 0U || ext->len != 9U + count * 4U) {
        return XGL_ERR_INVALID_FRAME;
    }
    cursor->value = ext->value;
    cursor->count = count;
    cursor->index = 0U;
    cursor->next_high = xgb_deserialize_u32_le(ext->value);
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
    if (cursor->index == cursor->count) {
        return XGL_ERR_NOT_FOUND;
    }
    const uint8_t* range = cursor->value + 9U + cursor->index * 4U;
    uint32_t gap = xgb_deserialize_u16_le(range);
    uint32_t length = xgb_deserialize_u16_le(range + 2U);
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
void transport_acknowledge_packet(xgl_transport_ctx_t* ctx,
                                  xgl_transport_peer_state_t* peer,
                                  uint32_t packet_number, uint32_t now_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    (void)ctx;
    const xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(
        &peer->reliable_queue, packet_number, peer->peer_id);
    if (packet == NULL || !packet->sent) {
        return;
    }
    uint32_t elapsed = now_ms - packet->send_timestamp;
    if (packet->retry_count == 0U && elapsed > 0U && elapsed <= INT32_MAX) {
        xgl_rtt_update(&peer->rtt_est, (int32_t)elapsed);
    }
    (void)xgl_window_mark_ack_packet_number(&peer->tx_window, packet_number);
    (void)xgl_reliable_remove_packet_number(&peer->reliable_queue,
                                            packet_number, peer->peer_id);
}

/**
 * \brief           Validate every ACK range before changing queue or window
 *                  state
 * \param[in,out]   ctx: Transport layer context
 * \param[in,out]   peer: Exact peer and session scope
 * \param[in]       source_id: Sender node ID
 * \param[in]       data: Complete extension list
 * \param[in]       data_len: Extension list length
 * \param[out]      handled: Whether an ACK range extension was processed
 * \return          XGL_OK or an ACK validation error
 */
xgl_error_t transport_try_process_ack_range_ext(
    xgl_transport_ctx_t* ctx, xgl_transport_peer_state_t* peer,
    uint16_t source_id, const uint8_t* data, size_t data_len, bool* handled) {
    if (ctx == NULL || peer == NULL || handled == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *handled = false;
    if (source_id != peer->peer_id) {
        return XGL_ERR_SEQUENCE_ERROR;
    }
    xgl_wire_ext_cursor_t ext_cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&ext_cursor, data, data_len);
    if (err != XGL_OK) {
        return err;
    }
    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&ext_cursor, &ext)) == XGL_OK) {
        if (ext.type != XGL_WIRE_EXT_ACK_RANGE) {
            continue;
        }
        ack_ranges_t ranges;
        err = ack_ranges_init(&ranges, &ext);
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

        /* Validate before mutation, then visit only actual outstanding packets.
         * Duplicate ACKs are no-ops, and huge absent ranges cost no per-number
         * work. */
        uint32_t now = transport_now(ctx);
        xgct_list_node_t *node, *next;
        XGCT_LIST_FOR_EACH_SAFE(&peer->reliable_queue.wait_ack_list, node,
                                next) {
            const xgl_reliable_packet_t* packet =
                /* Intrusive node membership is established by the owning list.
                 */
                /* NOLINTNEXTLINE(bugprone-casting-through-void) */
                XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
            (void)ack_ranges_init(&ranges, &ext);
            while (ack_ranges_next(&ranges, &low, &high) == XGL_OK) {
                if (packet->packet_number >= low &&
                    packet->packet_number <= high) {
                    transport_acknowledge_packet(ctx, peer,
                                                 packet->packet_number, now);
                    break;
                }
            }
        }
        (void)xgl_window_advance_base_packet_number(&peer->tx_window);

        peer->last_active_ms = now;
        *handled = true;
        return XGL_OK;
    }
    return err == XGL_ERR_NOT_FOUND ? XGL_OK : err;
}
