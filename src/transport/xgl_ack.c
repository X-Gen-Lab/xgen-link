/**
 * \file            xgl_ack.c
 * \brief           ACK/NACK Packet Handling Implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_ack.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_sequence.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_wire.h>
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

struct xgl_ack_peer_state {
    struct xgl_ack_peer_state* next;
    uint16_t source_id;
    uint8_t* received_seq_bitmap;
    uint8_t expected_seq_num;
};

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
    bitmap[byte_index] |= (uint8_t)(1U << bit_offset);
}

/**
 * \brief           Get bit from bitmap
 */
static bool get_bit(const uint8_t* bitmap, uint8_t bit_index) {
    size_t byte_index = bit_index / 8;
    uint8_t bit_offset = bit_index % 8;
    return (bitmap[byte_index] & (uint8_t)(1U << bit_offset)) != 0;
}

static xgl_ack_peer_state_t* find_peer_state(const xgl_ack_handler_t* handler,
                                             uint16_t source_id) {
    if (handler == NULL) {
        return NULL;
    }

    xgl_ack_peer_state_t* peer = handler->peers;
    while (peer != NULL) {
        if (peer->source_id == source_id) {
            return peer;
        }
        peer = peer->next;
    }

    return NULL;
}

static xgl_ack_peer_state_t* get_or_create_peer_state(xgl_ack_handler_t* handler,
                                                      uint16_t source_id) {
    xgl_ack_peer_state_t* peer = find_peer_state(handler, source_id);
    if (peer != NULL) {
        return peer;
    }

    peer = (xgl_ack_peer_state_t*)ack_malloc(handler->allocator,
                                             sizeof(xgl_ack_peer_state_t));
    if (peer == NULL) {
        return NULL;
    }

    peer->received_seq_bitmap = (uint8_t*)ack_malloc(handler->allocator,
                                                     XGL_ACK_BITMAP_SIZE);
    if (peer->received_seq_bitmap == NULL) {
        ack_free(handler->allocator, peer);
        return NULL;
    }

    memset(peer->received_seq_bitmap, 0, XGL_ACK_BITMAP_SIZE);
    peer->expected_seq_num = 0;
    peer->source_id = source_id;
    peer->next = handler->peers;
    handler->peers = peer;

    return peer;
}

static bool is_out_of_order_for_expected(uint8_t expected_seq_num,
                                         uint8_t seq_num) {
    if (seq_num == expected_seq_num) {
        return false;
    }

    int16_t diff = xgl_sequence_diff(seq_num, expected_seq_num);
    if (diff > 0 && diff < 128) {
        return true;
    }

    if (diff < 0 && diff > -128) {
        return true;
    }

    return false;
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

    memset(handler, 0, sizeof(xgl_ack_handler_t));
    
    /* Allocate bitmap for tracking received sequence numbers */
    handler->bitmap_size = XGL_ACK_BITMAP_SIZE;
    handler->allocator = allocator;
    handler->received_seq_bitmap = (uint8_t*)ack_malloc(allocator, handler->bitmap_size);
    
    if (handler->received_seq_bitmap == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    
    /* Initialize bitmap to zero */
    memset(handler->received_seq_bitmap, 0, handler->bitmap_size);
    
    /* Initialize expected sequence number */
    handler->expected_seq_num = 0;
    
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

    xgl_ack_peer_state_t* peer = handler->peers;
    while (peer != NULL) {
        xgl_ack_peer_state_t* next = peer->next;
        ack_free(handler->allocator, peer->received_seq_bitmap);
        ack_free(handler->allocator, peer);
        peer = next;
    }
    handler->peers = NULL;
    
    handler->bitmap_size = 0;
}

/**
 * \brief           Generate ACK packet
 */
xgl_error_t xgl_ack_generate(uint8_t seq_num,
                             uint16_t source_id,
                             uint16_t target_id,
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
    
    xgl_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.sof = XGL_SOF;
    xgl_frame_set_version(&header, 1);
    xgl_frame_set_datatype(&header, XGL_PACKET_TYPE_ACK);
    header.source_id = target_id;
    header.target_id = source_id;
    header.attr_lsb = XGL_ATTR_RELIABLE_ACK;
    header.seq_num = seq_num;
    header.ack_num = seq_num;

    xgl_frame_encode_header(ack_buffer, &header);
    
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
                            uint16_t source_id,
                            bool* is_valid) {
    if (handler == NULL || is_valid == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    (void)ack_num;
    (void)source_id;

    if (handler->received_seq_bitmap == NULL) {
        *is_valid = false;
        return XGL_ERR_NOT_INITIALIZED;
    }

    *is_valid = true;
    
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

bool xgl_ack_is_duplicate_from(const xgl_ack_handler_t* handler,
                               uint16_t source_id,
                               uint8_t seq_num) {
    xgl_ack_peer_state_t* peer = find_peer_state(handler, source_id);
    if (peer == NULL || peer->received_seq_bitmap == NULL) {
        return false;
    }

    return get_bit(peer->received_seq_bitmap, seq_num);
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

xgl_error_t xgl_ack_mark_received_from(xgl_ack_handler_t* handler,
                                       uint16_t source_id,
                                       uint8_t seq_num) {
    if (handler == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (handler->received_seq_bitmap == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    xgl_ack_peer_state_t* peer = get_or_create_peer_state(handler, source_id);
    if (peer == NULL || peer->received_seq_bitmap == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    set_bit(peer->received_seq_bitmap, seq_num);

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

    return is_out_of_order_for_expected(handler->expected_seq_num, seq_num);
}

bool xgl_ack_is_out_of_order_from(const xgl_ack_handler_t* handler,
                                  uint16_t source_id,
                                  uint8_t seq_num) {
    if (handler == NULL) {
        return false;
    }

    xgl_ack_peer_state_t* peer = find_peer_state(handler, source_id);
    uint8_t expected_seq_num = (peer != NULL) ? peer->expected_seq_num : 0;

    return is_out_of_order_for_expected(expected_seq_num, seq_num);
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
    handler->expected_seq_num = (uint8_t)(seq_num + 1U);
}

xgl_error_t xgl_ack_update_expected_from(xgl_ack_handler_t* handler,
                                         uint16_t source_id,
                                         uint8_t seq_num) {
    if (handler == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (handler->received_seq_bitmap == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    xgl_ack_peer_state_t* peer = get_or_create_peer_state(handler, source_id);
    if (peer == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    peer->expected_seq_num = (uint8_t)(seq_num + 1U);

    return XGL_OK;
}

uint8_t xgl_ack_get_expected_from(const xgl_ack_handler_t* handler,
                                  uint16_t source_id) {
    if (handler == NULL) {
        return 0;
    }

    xgl_ack_peer_state_t* peer = find_peer_state(handler, source_id);
    return (peer != NULL) ? peer->expected_seq_num : 0;
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

    xgl_ack_peer_state_t* peer = handler->peers;
    while (peer != NULL) {
        if (peer->received_seq_bitmap != NULL) {
            memset(peer->received_seq_bitmap, 0, XGL_ACK_BITMAP_SIZE);
        }
        peer->expected_seq_num = 0;
        peer = peer->next;
    }
    
    /* Reset expected sequence number */
    handler->expected_seq_num = 0;
}
