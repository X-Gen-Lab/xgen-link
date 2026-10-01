/**
 * \file            xgl_window.c
 * \brief           Sliding Window Implementation
 * \author          X-Gen Lab
 */

#include "transport/xgl_window.h"

#include <string.h>
#include <xgen/containers/bitset.h>

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

    if (window_size == 0 || window_size > 128) {
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

    xgct_bitset_t bits = {window->ack_received, window->window_size};
    xgct_bitset_clear_all(&bits);
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
    xgct_bitset_t bits = {window->ack_received, window->window_size};
    (void)xgct_bitset_set(&bits, index);

    return XGL_OK;
}

uint8_t xgl_window_advance_base_packet_number(xgl_sliding_window_t* window) {
    if (window == NULL || window->window_size == 0U) {
        return 0U;
    }

    uint8_t advanced = 0U;
    xgct_bitset_t bits = {window->ack_received, window->window_size};
    while (advanced < window->window_size &&
           xgct_bitset_test(&bits, window->ack_head)) {
        (void)xgct_bitset_clear(&bits, window->ack_head);
        ++window->ack_head;
        if (window->ack_head == window->window_size) {
            window->ack_head = 0U;
        }
        window->send_base_packet_number++;
        advanced++;
    }

    return advanced;
}
