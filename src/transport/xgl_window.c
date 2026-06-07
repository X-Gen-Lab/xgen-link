/**
 * \file            xgl_window.c
 * \brief           Sliding Window Implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_window.h"
#include <stdlib.h>
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
    
    if (window_size == 0 || window_size > 128) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Allocate ACK bitmap */
    window->ack_received = (bool*)calloc(window_size, sizeof(bool));
    if (window->ack_received == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    
    /* Initialize window state */
    window->window_size = window_size;
    window->send_base_packet_number = 0;
    window->next_packet_number = 0;
    
    return XGL_OK;
}

/**
 * \brief           Destroy sliding window and free resources
 */
void xgl_window_destroy(xgl_sliding_window_t* window) {
    if (window == NULL) {
        return;
    }
    
    if (window->ack_received != NULL) {
        free(window->ack_received);
        window->ack_received = NULL;
    }
}

/**
 * \brief           Check if window allows sending
 */
bool xgl_window_can_send(const xgl_sliding_window_t* window) {
    return xgl_window_can_send_packet_number(window);
}

/**
 * \brief           Advance window base on ACK reception
 */
uint8_t xgl_window_advance_base(xgl_sliding_window_t* window) {
    return xgl_window_advance_base_packet_number(window);
}

/**
 * \brief           Get current window usage
 */
uint8_t xgl_window_get_usage(const xgl_sliding_window_t* window) {
    if (window == NULL) {
        return 0;
    }

    uint32_t usage = window->next_packet_number - window->send_base_packet_number;
    if (usage > UINT8_MAX) {
        return UINT8_MAX;
    }

    return (uint8_t)usage;
}

/**
 * \brief           Reset sliding window to initial state
 */
void xgl_window_reset(xgl_sliding_window_t* window) {
    if (window == NULL || window->ack_received == NULL) {
        return;
    }
    
    /* Clear all ACK flags */
    memset(window->ack_received, 0, window->window_size * sizeof(bool));

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

    if (window->ack_received == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    if (!xgl_window_is_in_window_packet_number(window, packet_number)) {
        return XGL_ERR_SEQUENCE_ERROR;
    }

    uint32_t index = packet_number - window->send_base_packet_number;
    window->ack_received[index] = true;

    return XGL_OK;
}

uint8_t xgl_window_advance_base_packet_number(xgl_sliding_window_t* window) {
    if (window == NULL || window->ack_received == NULL) {
        return 0U;
    }

    uint8_t advanced = 0U;
    while (advanced < window->window_size && window->ack_received[0]) {
        for (uint8_t i = 1U; i < window->window_size; ++i) {
            window->ack_received[i - 1U] = window->ack_received[i];
        }
        window->ack_received[window->window_size - 1U] = false;
        window->send_base_packet_number++;
        advanced++;
    }

    return advanced;
}
