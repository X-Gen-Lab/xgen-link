/**
 * \file            xgl_security.c
 * \brief           Security helpers for authenticated production transport
 */

#include <xgl/internal/xgl_security.h>
#include <string.h>

xgl_error_t xgl_replay_window_init(xgl_replay_window_t* window,
                                   uint16_t source_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch,
                                   uint8_t window_size) {
    if (window == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (source_id == 0U || window_size == 0U || window_size > 64U) {
        return XGL_ERR_INVALID_PARAM;
    }

    memset(window, 0, sizeof(*window));
    window->source_id = source_id;
    window->connection_id = connection_id;
    window->session_epoch = session_epoch;
    window->window_size = window_size;

    return XGL_OK;
}

bool xgl_replay_window_accept(xgl_replay_window_t* window,
                              uint16_t source_id,
                              uint32_t connection_id,
                              uint32_t session_epoch,
                              uint32_t packet_number) {
    if (window == NULL) {
        return false;
    }

    if (source_id != window->source_id ||
        connection_id != window->connection_id ||
        session_epoch != window->session_epoch) {
        return false;
    }

    if (!window->has_largest) {
        window->largest_packet_number = packet_number;
        window->received_bitmap = 1U;
        window->has_largest = true;
        return true;
    }

    if (packet_number > window->largest_packet_number) {
        uint32_t diff = packet_number - window->largest_packet_number;
        if (diff >= 64U) {
            window->received_bitmap = 1U;
        } else {
            window->received_bitmap <<= diff;
            window->received_bitmap |= 1U;
        }
        window->largest_packet_number = packet_number;
        return true;
    }

    uint32_t offset = window->largest_packet_number - packet_number;
    if (offset >= window->window_size || offset >= 64U) {
        return false;
    }

    uint64_t bit = 1ULL << offset;
    if ((window->received_bitmap & bit) != 0U) {
        return false;
    }

    window->received_bitmap |= bit;
    return true;
}
