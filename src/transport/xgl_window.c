/**
 * \file            xgl_window.c
 * \brief           Sliding Window Implementation
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "transport/xgl_window.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* Public Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize sliding window
 */
xgl_error_t xgl_window_init(xgl_sliding_window_t* window, uint8_t window_size) {
    if (window == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (window_size == 0 || window_size > XGL_BUILD_WINDOW_CAPACITY) {
        return XGL_ERR_INVALID_PARAM;
    }

    *window = (xgl_sliding_window_t){
        .window_size = window_size,
        .ack_head = 0U,
        .send_base_packet_number = 0U,
        .next_packet_number = 0U,
    };

    return XGL_OK;
}

/**
 * \brief           Clear sliding window state and inline storage
 */
void xgl_window_destroy(xgl_sliding_window_t* window) {
    if (window == NULL) {
        return;
    }

    memset(window, 0, sizeof(*window));
}

/**
 * \brief           Get current window usage
 */
uint8_t xgl_window_get_usage(const xgl_sliding_window_t* window) {
    if (window == NULL) {
        return 0;
    }

    uint32_t usage =
        window->next_packet_number - window->send_base_packet_number;
    if (usage > UINT8_MAX) {
        return UINT8_MAX;
    }

    return (uint8_t)usage;
}

/**
 * \brief           Reset sliding window to initial state
 */
void xgl_window_reset(xgl_sliding_window_t* window) {
    if (window == NULL || window->window_size == 0U) {
        return;
    }

    const size_t bitmap_bytes = ((size_t)window->window_size + 7U) / 8U;
    memset(window->ack_received, 0, bitmap_bytes);
    window->ack_head = 0U;

    window->send_base_packet_number = 0;
    window->next_packet_number = 0;
}

bool xgl_window_can_send_packet_number(const xgl_sliding_window_t* window) {
    if (window == NULL) {
        return false;
    }

    return (window->next_packet_number - window->send_base_packet_number) <
           window->window_size;
}

uint32_t xgl_window_get_next_packet_number(const xgl_sliding_window_t* window) {
    if (window == NULL) {
        return 0U;
    }

    return window->next_packet_number;
}

void xgl_window_advance_next_packet_number(xgl_sliding_window_t* window) {
    if (window == NULL) {
        return;
    }

    window->next_packet_number++;
}

bool xgl_window_is_in_window_packet_number(const xgl_sliding_window_t* window,
                                           uint32_t packet_number) {
    if (window == NULL || packet_number < window->send_base_packet_number) {
        return false;
    }

    return (packet_number - window->send_base_packet_number) <
           window->window_size;
}

xgl_error_t xgl_window_mark_ack_packet_number(xgl_sliding_window_t* window,
                                              uint32_t packet_number) {
    if (window == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (window->window_size == 0U) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    if (!xgl_window_is_in_window_packet_number(window, packet_number)) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    uint32_t index =
        window->ack_head + (packet_number - window->send_base_packet_number);
    if (index >= window->window_size) {
        index -= window->window_size;
    }
    if (index < window->window_size) {
        window->ack_received[index / 8U] |= (uint8_t)(1U << (index % 8U));
    }

    return XGL_OK;
}

uint8_t xgl_window_advance_base_packet_number(xgl_sliding_window_t* window) {
    if (window == NULL || window->window_size == 0U) {
        return 0U;
    }

    uint8_t advanced = 0U;
    while (advanced < window->window_size &&
           window->ack_head < window->window_size) {
        uint8_t* byte = &window->ack_received[window->ack_head / 8U];
        const uint8_t mask = (uint8_t)(1U << (window->ack_head % 8U));
        if ((*byte & mask) == 0U) {
            break;
        }
        *byte &= (uint8_t)~mask;
        ++window->ack_head;
        if (window->ack_head == window->window_size) {
            window->ack_head = 0U;
        }
        window->send_base_packet_number++;
        advanced++;
    }

    return advanced;
}
