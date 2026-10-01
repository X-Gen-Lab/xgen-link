/**
 * \file            xgl_window.h
 * \brief           Sliding Window for Flow Control
 * \author          X-Gen Lab
 */

#ifndef XGL_WINDOW_H
#define XGL_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Sliding Window Structure                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Sliding window structure for flow control
 * \note            Implements reliable transmission with ACK tracking and
 *                  window advancement. The caller serializes all operations.
 */
typedef struct {
    uint8_t window_size; /**< Maximum window size */
    uint8_t ack_head;    /**< Physical bit corresponding to the window base */
    uint32_t
        send_base_packet_number; /**< Base packet number of sending window */
    uint32_t next_packet_number; /**< Next packet number to send */
    uint8_t* ack_received;       /**< Packed ACK storage owned by allocator */
    const xgm_allocator_t* allocator; /**< Allocator used for ACK bitmap */
} xgl_sliding_window_t;

/*---------------------------------------------------------------------------*/
/* Sliding Window Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize sliding window with an explicit allocator
 * \param[in,out]   window: Uninitialized or previously destroyed window
 * \param[in]       window_size: Maximum window size, from 1 to 128
 * \param[in]       allocator: Borrowed allocator descriptor and context that
 *                  must remain valid until window destruction
 * \return          XGL_OK on success, error code otherwise
 * \note            Destroy a live window before initializing it again.
 *                  Failure leaves the supplied window unchanged. The window
 *                  and allocator descriptor must be separate stable objects.
 */
xgl_error_t xgl_window_init_with_allocator(xgl_sliding_window_t* window,
                                           uint8_t window_size,
                                           const xgm_allocator_t* allocator);

/**
 * \brief           Destroy sliding window and free resources
 * \param[in,out]   window: Sliding window structure
 * \note            Call only after successful initialization or on a
 *                  zero-initialized window. Repeated destruction is safe.
 */
void xgl_window_destroy(xgl_sliding_window_t* window);

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

bool xgl_window_can_send_packet_number(const xgl_sliding_window_t* window);

uint32_t xgl_window_get_next_packet_number(const xgl_sliding_window_t* window);

void xgl_window_advance_next_packet_number(xgl_sliding_window_t* window);

xgl_error_t xgl_window_mark_ack_packet_number(xgl_sliding_window_t* window,
                                              uint32_t packet_number);

uint8_t xgl_window_advance_base_packet_number(xgl_sliding_window_t* window);

bool xgl_window_is_in_window_packet_number(const xgl_sliding_window_t* window,
                                           uint32_t packet_number);

#ifdef __cplusplus
}
#endif

#endif /* XGL_WINDOW_H */
