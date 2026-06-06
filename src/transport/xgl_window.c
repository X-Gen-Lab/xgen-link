/**
 * \file            xgl_window.c
 * \brief           Sliding Window Implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_window.h"
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Calculate sequence number difference (handles wraparound)
 * \param[in]       a: First sequence number
 * \param[in]       b: Second sequence number
 * \return          Difference (a - b) with wraparound handling
 */
static inline uint8_t seq_diff(uint8_t a, uint8_t b) {
    return (uint8_t)(a - b);
}

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
    window->ack_received = (bool*)calloc(256, sizeof(bool));
    if (window->ack_received == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    
    /* Initialize window state */
    window->window_size = window_size;
    window->send_base = 0;
    window->next_seq_num = 0;
    window->expected_seq_num = 0;
    
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
    if (window == NULL) {
        return false;
    }
    
    /* Check if next_seq_num is within window from send_base */
    uint8_t diff = seq_diff(window->next_seq_num, window->send_base);
    return diff < window->window_size;
}

/**
 * \brief           Get next sequence number to send
 */
uint8_t xgl_window_get_next_seq(const xgl_sliding_window_t* window) {
    if (window == NULL) {
        return 0;
    }
    
    return window->next_seq_num;
}

/**
 * \brief           Advance next sequence number after sending
 */
void xgl_window_advance_next_seq(xgl_sliding_window_t* window) {
    if (window == NULL) {
        return;
    }
    
    window->next_seq_num++;
}

/**
 * \brief           Mark ACK as received for a sequence number
 */
xgl_error_t xgl_window_mark_ack(xgl_sliding_window_t* window, uint8_t seq_num) {
    if (window == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (window->ack_received == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
    /* Check if sequence number is within current window */
    if (!xgl_window_is_in_window(window, seq_num)) {
        return XGL_ERR_SEQUENCE_ERROR;
    }
    
    /* Mark ACK as received */
    window->ack_received[seq_num] = true;
    
    return XGL_OK;
}

/**
 * \brief           Advance window base on ACK reception
 */
uint8_t xgl_window_advance_base(xgl_sliding_window_t* window) {
    if (window == NULL || window->ack_received == NULL) {
        return 0;
    }
    
    uint16_t advanced = 0;
    
    /* Advance base while consecutive ACKs are received */
    while (window->ack_received[window->send_base]) {
        /* Clear ACK flag */
        window->ack_received[window->send_base] = false;
        
        /* Advance base */
        window->send_base++;
        advanced++;
        
        /* Prevent infinite loop (should not happen in practice) */
        if (advanced >= 256) {
            break;
        }
    }
    
    return (uint8_t)advanced;
}

/**
 * \brief           Check if sequence number is within window
 */
bool xgl_window_is_in_window(const xgl_sliding_window_t* window, uint8_t seq_num) {
    if (window == NULL) {
        return false;
    }
    
    /* Calculate difference from send_base */
    uint8_t diff = seq_diff(seq_num, window->send_base);
    
    /* Check if within window size */
    return diff < window->window_size;
}

/**
 * \brief           Get current window usage
 */
uint8_t xgl_window_get_usage(const xgl_sliding_window_t* window) {
    if (window == NULL) {
        return 0;
    }
    
    /* Calculate number of unacknowledged packets */
    return seq_diff(window->next_seq_num, window->send_base);
}

/**
 * \brief           Reset sliding window to initial state
 */
void xgl_window_reset(xgl_sliding_window_t* window) {
    if (window == NULL || window->ack_received == NULL) {
        return;
    }
    
    /* Clear all ACK flags */
    memset(window->ack_received, 0, 256 * sizeof(bool));
    
    /* Reset sequence numbers */
    window->send_base = 0;
    window->next_seq_num = 0;
    window->expected_seq_num = 0;
}

/**
 * \brief           Check if ACK was received for a sequence number
 */
bool xgl_window_is_acked(const xgl_sliding_window_t* window, uint8_t seq_num) {
    if (window == NULL || window->ack_received == NULL) {
        return false;
    }
    
    return window->ack_received[seq_num];
}
