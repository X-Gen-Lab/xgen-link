/**
 * \file            xgl_window.h
 * \brief           Sliding Window for Flow Control
 * \author          Nexus Team
 */

#ifndef XGL_WINDOW_H
#define XGL_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Sliding Window Structure                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Sliding window structure for flow control
 * \note            Implements a sliding window protocol for reliable transmission
 *                  with ACK tracking and window advancement
 */
typedef struct {
    uint8_t window_size;            /**< Maximum window size */
    uint8_t send_base;              /**< Base of sending window */
    uint8_t next_seq_num;           /**< Next sequence number to send */
    uint8_t expected_seq_num;       /**< Expected sequence number for receive */
    bool* ack_received;             /**< ACK bitmap (dynamically allocated) */
} xgl_sliding_window_t;

/*---------------------------------------------------------------------------*/
/* Sliding Window Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize sliding window
 * \param[in,out]   window: Sliding window structure
 * \param[in]       window_size: Maximum window size
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_window_init(xgl_sliding_window_t* window, uint8_t window_size);

/**
 * \brief           Destroy sliding window and free resources
 * \param[in,out]   window: Sliding window structure
 */
void xgl_window_destroy(xgl_sliding_window_t* window);

/**
 * \brief           Check if window allows sending
 * \param[in]       window: Sliding window structure
 * \return          true if can send, false if window is full
 */
bool xgl_window_can_send(const xgl_sliding_window_t* window);

/**
 * \brief           Get next sequence number to send
 * \param[in]       window: Sliding window structure
 * \return          Next sequence number
 */
uint8_t xgl_window_get_next_seq(const xgl_sliding_window_t* window);

/**
 * \brief           Advance next sequence number after sending
 * \param[in,out]   window: Sliding window structure
 */
void xgl_window_advance_next_seq(xgl_sliding_window_t* window);

/**
 * \brief           Mark ACK as received for a sequence number
 * \param[in,out]   window: Sliding window structure
 * \param[in]       seq_num: Sequence number that was acknowledged
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_window_mark_ack(xgl_sliding_window_t* window, uint8_t seq_num);

/**
 * \brief           Advance window base on ACK reception
 * \param[in,out]   window: Sliding window structure
 * \return          Number of positions advanced
 */
uint8_t xgl_window_advance_base(xgl_sliding_window_t* window);

/**
 * \brief           Check if sequence number is within window
 * \param[in]       window: Sliding window structure
 * \param[in]       seq_num: Sequence number to check
 * \return          true if within window, false otherwise
 */
bool xgl_window_is_in_window(const xgl_sliding_window_t* window, uint8_t seq_num);

/**
 * \brief           Get current window usage
 * \param[in]       window: Sliding window structure
 * \return          Number of unacknowledged packets in window
 */
uint8_t xgl_window_get_usage(const xgl_sliding_window_t* window);

/**
 * \brief           Reset sliding window to initial state
 * \param[in,out]   window: Sliding window structure
 */
void xgl_window_reset(xgl_sliding_window_t* window);

/**
 * \brief           Check if ACK was received for a sequence number
 * \param[in]       window: Sliding window structure
 * \param[in]       seq_num: Sequence number to check
 * \return          true if ACK received, false otherwise
 */
bool xgl_window_is_acked(const xgl_sliding_window_t* window, uint8_t seq_num);

#ifdef __cplusplus
}
#endif

#endif /* XGL_WINDOW_H */
