/**
 * \file            xgl_wire.h
 * \brief           Production wire-format encoding primitives
 */

#ifndef XGL_WIRE_H
#define XGL_WIRE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Wire Header Constants                                                      */
/*---------------------------------------------------------------------------*/

#define XGL_WIRE_MAGIC_0            ((uint8_t)'X')
#define XGL_WIRE_MAGIC_1            ((uint8_t)'G')
#define XGL_WIRE_VERSION            2U
#define XGL_WIRE_BASE_HEADER_SIZE   24U
#define XGL_WIRE_EXT_HEADER_SIZE    2U

typedef enum {
    XGL_PACKET_TYPE_INVALID = 0,
    XGL_PACKET_TYPE_DATA = 1,
    XGL_PACKET_TYPE_ACK = 2,
    XGL_PACKET_TYPE_CONTROL = 3,
    XGL_PACKET_TYPE_HANDSHAKE = 4,
    XGL_PACKET_TYPE_ROUTE = 5,
    XGL_PACKET_TYPE_PROBE = 6,
    XGL_PACKET_TYPE_CLOSE = 7
} xgl_packet_type_t;

#define XGL_WIRE_FLAG_ACK_ELICITING   0x01U
#define XGL_WIRE_FLAG_HAS_EXTENSIONS  0x02U
#define XGL_WIRE_FLAG_FRAGMENTED      0x04U
#define XGL_WIRE_FLAG_ENCRYPTED       0x08U
#define XGL_WIRE_FLAG_AUTHENTICATED   0x10U
#define XGL_WIRE_FLAG_CONTROL         0x20U

typedef enum {
    XGL_WIRE_EXT_SESSION = 1,
    XGL_WIRE_EXT_ACK_RANGE = 2,
    XGL_WIRE_EXT_SACK = 3,
    XGL_WIRE_EXT_FRAGMENT = 4,
    XGL_WIRE_EXT_SECURITY = 5,
    XGL_WIRE_EXT_ROUTE = 6,
    XGL_WIRE_EXT_TIMESTAMP = 7
} xgl_wire_ext_type_t;

/*---------------------------------------------------------------------------*/
/* Logical Wire Structures                                                    */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint8_t version;
    uint8_t header_len;
    uint8_t packet_type;
    uint8_t flags;
    uint8_t ttl;
    uint8_t traffic_class;
    uint16_t source_id;
    uint16_t target_id;
    uint32_t connection_id;
    uint32_t packet_number;
    uint16_t payload_len;
    uint16_t header_crc16;
} xgl_wire_header_t;

typedef struct {
    uint8_t type;
    size_t len;
    const uint8_t* value;
    bool valid;
} xgl_wire_ext_t;

typedef struct {
    uint16_t gap;
    uint16_t length;
} xgl_wire_ack_range_t;

typedef struct {
    const uint8_t* buffer;
    size_t len;
    size_t offset;
} xgl_wire_ext_cursor_t;

/*---------------------------------------------------------------------------*/
/* Wire Encoding API                                                          */
/*---------------------------------------------------------------------------*/

xgl_error_t xgl_wire_encode_header(uint8_t* buffer,
                                   size_t buffer_size,
                                   const xgl_wire_header_t* header);

xgl_error_t xgl_wire_decode_header(xgl_wire_header_t* header,
                                   const uint8_t* buffer,
                                   size_t buffer_size);

xgl_error_t xgl_wire_encode_ext(uint8_t* buffer,
                                size_t buffer_size,
                                uint8_t type,
                                const uint8_t* value,
                                size_t value_len,
                                size_t* bytes_written);

xgl_error_t xgl_wire_ext_cursor_init(xgl_wire_ext_cursor_t* cursor,
                                     const uint8_t* buffer,
                                     size_t len);

xgl_error_t xgl_wire_ext_cursor_next(xgl_wire_ext_cursor_t* cursor,
                                     xgl_wire_ext_t* ext);

xgl_error_t xgl_wire_encode_ack_range_ext_value(uint8_t* buffer,
                                                size_t buffer_size,
                                                uint32_t largest_ack,
                                                uint32_t ack_delay_us,
                                                const xgl_wire_ack_range_t* ranges,
                                                size_t range_count,
                                                size_t* bytes_written);

xgl_error_t xgl_wire_decode_ack_range_ext_value(const uint8_t* buffer,
                                                size_t buffer_size,
                                                uint32_t* largest_ack,
                                                uint32_t* ack_delay_us,
                                                xgl_wire_ack_range_t* ranges,
                                                size_t range_capacity,
                                                size_t* range_count);

xgl_error_t xgl_wire_encode_sack_ext_value(uint8_t* buffer,
                                           size_t buffer_size,
                                           uint32_t base_packet,
                                           const uint8_t* bitmap,
                                           size_t bitmap_len,
                                           size_t* bytes_written);

xgl_error_t xgl_wire_decode_sack_ext_value(const uint8_t* buffer,
                                           size_t buffer_size,
                                           uint32_t* base_packet,
                                           uint8_t* bitmap,
                                           size_t bitmap_capacity,
                                           size_t* bitmap_len);

xgl_error_t xgl_wire_encode_fragment_ext_value(uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t message_id,
                                               uint32_t fragment_offset,
                                               uint32_t message_len,
                                               size_t* bytes_written);

xgl_error_t xgl_wire_decode_fragment_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* message_id,
                                               uint32_t* fragment_offset,
                                               uint32_t* message_len);

xgl_error_t xgl_wire_encode_session_ext_value(uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t session_epoch,
                                              uint64_t incarnation_id,
                                              size_t* bytes_written);

xgl_error_t xgl_wire_decode_session_ext_value(const uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t* session_epoch,
                                              uint64_t* incarnation_id);

xgl_error_t xgl_wire_encode_security_ext_value(uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t key_id,
                                               uint64_t nonce_id,
                                               uint8_t tag_len,
                                               size_t* bytes_written);

xgl_error_t xgl_wire_decode_security_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* key_id,
                                               uint64_t* nonce_id,
                                               uint8_t* tag_len);

xgl_error_t xgl_wire_encode_route_ext_value(uint8_t* buffer,
                                            size_t buffer_size,
                                            uint16_t previous_hop,
                                            uint16_t next_hop,
                                            uint32_t route_epoch,
                                            uint16_t metric,
                                            size_t* bytes_written);

xgl_error_t xgl_wire_decode_route_ext_value(const uint8_t* buffer,
                                            size_t buffer_size,
                                            uint16_t* previous_hop,
                                            uint16_t* next_hop,
                                            uint32_t* route_epoch,
                                            uint16_t* metric);

#ifdef __cplusplus
}
#endif

#endif /* XGL_WIRE_H */
