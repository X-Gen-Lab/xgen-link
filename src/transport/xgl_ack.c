/**
 * \file            xgl_ack.c
 * \brief           ACK/NACK Packet Handling Implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_ack.h>
#include <xgl/xgl_sequence.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Constants                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Bitmap size for tracking 256 sequence numbers
 * \note            256 bits = 32 bytes
 */
#define XGL_ACK_BITMAP_SIZE         32

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using allocator or malloc
 */
static void* ack_malloc(xgl_allocator_t* allocator, size_t size) {
    if (allocator != NULL && allocator->malloc != NULL) {
        return allocator->malloc(size);
    }
    return malloc(size);
}

/**
 * \brief           Free memory using allocator or free
 */
static void ack_free(xgl_allocator_t* allocator, void* ptr) {
    if (ptr == NULL) {
        return;
    }
    
    if (allocator != NULL && allocator->free != NULL) {
        allocator->free(ptr);
    } else {
        free(ptr);
    }
}

/**
 * \brief           Set bit in bitmap
 */
static void set_bit(uint8_t* bitmap, uint8_t bit_index) {
    size_t byte_index = bit_index / 8;
    uint8_t bit_offset = bit_index % 8;
    bitmap[byte_index] |= (1 << bit_offset);
}

/**
 * \brief           Get bit from bitmap
 */
static bool get_bit(const uint8_t* bitmap, uint8_t bit_index) {
    size_t byte_index = bit_index / 8;
    uint8_t bit_offset = bit_index % 8;
    return (bitmap[byte_index] & (1 << bit_offset)) != 0;
}

/**
 * \brief           Clear bit in bitmap
 */
static void clear_bit(uint8_t* bitmap, uint8_t bit_index) {
    size_t byte_index = bit_index / 8;
    uint8_t bit_offset = bit_index % 8;
    bitmap[byte_index] &= ~(1 << bit_offset);
}

/*---------------------------------------------------------------------------*/
/* ACK Handler Functions                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize ACK handler
 */
xgl_error_t xgl_ack_init(xgl_ack_handler_t* handler,
                         xgl_allocator_t* allocator) {
    if (handler == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Allocate bitmap for tracking received sequence numbers */
    handler->bitmap_size = XGL_ACK_BITMAP_SIZE;
    handler->received_seq_bitmap = (uint8_t*)ack_malloc(allocator, handler->bitmap_size);
    
    if (handler->received_seq_bitmap == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    
    /* Initialize bitmap to zero */
    memset(handler->received_seq_bitmap, 0, handler->bitmap_size);
    
    /* Initialize expected sequence number */
    handler->expected_seq_num = 0;
    
    /* Store allocator */
    handler->allocator = allocator;
    
    return XGL_OK;
}

/**
 * \brief           Destroy ACK handler
 */
void xgl_ack_destroy(xgl_ack_handler_t* handler) {
    if (handler == NULL) {
        return;
    }
    
    /* Free bitmap */
    if (handler->received_seq_bitmap != NULL) {
        ack_free(handler->allocator, handler->received_seq_bitmap);
        handler->received_seq_bitmap = NULL;
    }
    
    handler->bitmap_size = 0;
}

/**
 * \brief           Generate ACK packet
 */
xgl_error_t xgl_ack_generate(uint8_t seq_num,
                             uint8_t source_id,
                             uint8_t target_id,
                             uint8_t* ack_buffer,
                             size_t buffer_size,
                             size_t* ack_len) {
    if (ack_buffer == NULL || ack_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check buffer size (need at least frame header + CRC16) */
    if (buffer_size < (XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Build frame header */
    xgl_frame_header_t* header = (xgl_frame_header_t*)ack_buffer;
    
    /* Set SOF */
    header->sof = XGL_SOF;
    
    /* Set version (0) and data type (0 for ACK) */
    header->version_datatype = 0x00;
    
    /* Swap source and target for ACK */
    header->source_id = target_id;  /* ACK sender is original receiver */
    header->target_id = source_id;  /* ACK target is original sender */
    
    /* Set attributes - mark as ACK */
    header->attr_lsb = XGL_ATTR_RELIABLE_ACK;
    header->attr_msb = 0x00;
    
    /* Set data length (0 for ACK) */
    xgl_serialize_u16_le((uint8_t*)&header->data_len, 0);
    
    /* Set sequence number (not used in ACK) */
    header->seq_num = 0;
    
    /* Set ACK number */
    header->ack_num = seq_num;
    
    /* Set reserved byte */
    header->reserved = 0x00;
    
    /* Calculate CRC8 for header (exclude SOF and CRC8 itself) */
    header->crc8 = xgl_crc8_maxim((uint8_t*)&header->version_datatype,
                                  XGL_FRAME_HEADER_SIZE - 2);
    
    /* Calculate CRC16 for entire frame (header only, no payload) */
    uint16_t crc16 = xgl_crc16_modbus(ack_buffer, XGL_FRAME_HEADER_SIZE);
    
    /* Append CRC16 in little-endian */
    xgl_serialize_u16_le(ack_buffer + XGL_FRAME_HEADER_SIZE, crc16);
    
    /* Set output length */
    *ack_len = XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE;
    
    return XGL_OK;
}

/**
 * \brief           Process received ACK packet
 */
xgl_error_t xgl_ack_process(xgl_ack_handler_t* handler,
                            uint8_t ack_num,
                            uint8_t source_id,
                            bool* is_valid) {
    if (handler == NULL || is_valid == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Suppress unused parameter warnings */
    (void)ack_num;
    (void)source_id;
    
    /* Assume valid by default */
    *is_valid = true;
    
    /* ACK processing is primarily handled by reliable transmission queue */
    /* This function validates the ACK is within acceptable range */
    
    /* Check if ACK is for a sequence number we might have sent */
    /* Since sequence numbers wrap around, we accept any ACK */
    /* The reliable queue will determine if it matches a pending packet */
    
    return XGL_OK;
}

/**
 * \brief           Check if packet is duplicate
 */
bool xgl_ack_is_duplicate(const xgl_ack_handler_t* handler,
                          uint8_t seq_num) {
    if (handler == NULL || handler->received_seq_bitmap == NULL) {
        return false;
    }
    
    /* Check if this sequence number has been received before */
    return get_bit(handler->received_seq_bitmap, seq_num);
}

/**
 * \brief           Mark sequence number as received
 */
xgl_error_t xgl_ack_mark_received(xgl_ack_handler_t* handler,
                                  uint8_t seq_num) {
    if (handler == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (handler->received_seq_bitmap == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
    /* Set bit for this sequence number */
    set_bit(handler->received_seq_bitmap, seq_num);
    
    return XGL_OK;
}

/**
 * \brief           Check if packet is out-of-order
 */
bool xgl_ack_is_out_of_order(const xgl_ack_handler_t* handler,
                              uint8_t seq_num) {
    if (handler == NULL) {
        return false;
    }
    
    /* If seq_num equals expected, it's in order */
    if (seq_num == handler->expected_seq_num) {
        return false;
    }
    
    /* Use sequence number comparison that handles wraparound */
    int16_t diff = xgl_sequence_diff(seq_num, handler->expected_seq_num);
    
    /* If seq_num is ahead of expected (positive diff), it's out-of-order */
    if (diff > 0 && diff < 128) {
        return true;  /* Future packet within reasonable window */
    }
    
    /* If seq_num is behind expected (negative diff), it's out-of-order */
    if (diff < 0 && diff > -128) {
        return true;  /* Late packet within reasonable window */
    }
    
    /* Otherwise, it's too far away (likely from different cycle) */
    return false;
}

/**
 * \brief           Update expected sequence number
 */
void xgl_ack_update_expected(xgl_ack_handler_t* handler,
                             uint8_t seq_num) {
    if (handler == NULL) {
        return;
    }
    
    /* Update expected sequence number to next value */
    handler->expected_seq_num = seq_num + 1;
}

/**
 * \brief           Reset ACK handler state
 */
void xgl_ack_reset(xgl_ack_handler_t* handler) {
    if (handler == NULL) {
        return;
    }
    
    /* Clear bitmap */
    if (handler->received_seq_bitmap != NULL) {
        memset(handler->received_seq_bitmap, 0, handler->bitmap_size);
    }
    
    /* Reset expected sequence number */
    handler->expected_seq_num = 0;
}
