/**
 * \file            xgl_ack.h
 * \brief           ACK/NACK Packet Handling
 * \author          Nexus Team
 */

#ifndef XGL_ACK_H
#define XGL_ACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"
#include "xgl_types.h"

/*---------------------------------------------------------------------------*/
/* ACK Handler Structure                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           ACK handler structure
 * \note            Manages duplicate detection and out-of-order handling
 */
typedef struct {
    uint8_t* received_seq_bitmap;   /**< Bitmap of received sequence numbers */
    size_t bitmap_size;             /**< Size of bitmap in bytes */
    uint8_t expected_seq_num;       /**< Expected next sequence number */
    xgl_allocator_t* allocator;     /**< Memory allocator */
} xgl_ack_handler_t;

/*---------------------------------------------------------------------------*/
/* ACK Handler Functions                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize ACK handler
 * \param[in,out]   handler: ACK handler structure
 * \param[in]       allocator: Memory allocator (NULL = malloc/free)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_ack_init(xgl_ack_handler_t* handler,
                         xgl_allocator_t* allocator);

/**
 * \brief           Destroy ACK handler
 * \param[in,out]   handler: ACK handler structure
 */
void xgl_ack_destroy(xgl_ack_handler_t* handler);

/**
 * \brief           Generate ACK packet
 * \param[in]       seq_num: Sequence number to acknowledge
 * \param[in]       source_id: Source node ID (becomes target in ACK)
 * \param[in]       target_id: Target node ID (becomes source in ACK)
 * \param[out]      ack_buffer: Buffer to store ACK packet
 * \param[in]       buffer_size: Size of ACK buffer
 * \param[out]      ack_len: Length of generated ACK packet
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_ack_generate(uint8_t seq_num,
                             uint8_t source_id,
                             uint8_t target_id,
                             uint8_t* ack_buffer,
                             size_t buffer_size,
                             size_t* ack_len);

/**
 * \brief           Process received ACK packet
 * \param[in]       handler: ACK handler structure
 * \param[in]       ack_num: ACK number from received packet
 * \param[in]       source_id: Source node ID of ACK
 * \param[out]      is_valid: Set to true if ACK is valid
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_ack_process(xgl_ack_handler_t* handler,
                            uint8_t ack_num,
                            uint8_t source_id,
                            bool* is_valid);

/**
 * \brief           Check if packet is duplicate
 * \param[in]       handler: ACK handler structure
 * \param[in]       seq_num: Sequence number to check
 * \return          true if duplicate, false otherwise
 */
bool xgl_ack_is_duplicate(const xgl_ack_handler_t* handler,
                          uint8_t seq_num);

/**
 * \brief           Mark sequence number as received
 * \param[in,out]   handler: ACK handler structure
 * \param[in]       seq_num: Sequence number to mark
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_ack_mark_received(xgl_ack_handler_t* handler,
                                  uint8_t seq_num);

/**
 * \brief           Check if packet is out-of-order
 * \param[in]       handler: ACK handler structure
 * \param[in]       seq_num: Sequence number to check
 * \return          true if out-of-order, false otherwise
 */
bool xgl_ack_is_out_of_order(const xgl_ack_handler_t* handler,
                              uint8_t seq_num);

/**
 * \brief           Update expected sequence number
 * \param[in,out]   handler: ACK handler structure
 * \param[in]       seq_num: New expected sequence number
 */
void xgl_ack_update_expected(xgl_ack_handler_t* handler,
                             uint8_t seq_num);

/**
 * \brief           Reset ACK handler state
 * \param[in,out]   handler: ACK handler structure
 */
void xgl_ack_reset(xgl_ack_handler_t* handler);

#ifdef __cplusplus
}
#endif

#endif /* XGL_ACK_H */
