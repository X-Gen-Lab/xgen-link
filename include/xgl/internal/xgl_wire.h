/**
 * \file            xgl_wire.h
 * \brief           Production wire-format encoding primitives
 */

#ifndef XGL_WIRE_H
#define XGL_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Wire Header Constants                                                      */
/*---------------------------------------------------------------------------*/

#define XGL_WIRE_MAGIC_0            ((uint8_t)0xA5U)
#define XGL_WIRE_MAGIC_1            ((uint8_t)0x5AU)
#define XGL_WIRE_VERSION            3U
#define XGL_WIRE_BASE_HEADER_SIZE   24U
#define XGL_WIRE_EXT_HEADER_SIZE    2U
#define XGL_FRAGMENT_EXT_VALUE_SIZE 12U
#define XGL_FRAGMENT_EXT_SIZE                                                  \
    (XGL_WIRE_EXT_HEADER_SIZE + XGL_FRAGMENT_EXT_VALUE_SIZE)
#define XGL_SESSION_EXT_VALUE_SIZE 12U
#define XGL_SESSION_EXT_SIZE                                                   \
    (XGL_WIRE_EXT_HEADER_SIZE + XGL_SESSION_EXT_VALUE_SIZE)

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

#define XGL_WIRE_FLAG_ACK_ELICITING  0x01U
#define XGL_WIRE_FLAG_HAS_EXTENSIONS 0x02U
#define XGL_WIRE_FLAG_FRAGMENTED     0x04U
#define XGL_WIRE_FLAG_ENCRYPTED      0x08U
#define XGL_WIRE_FLAG_AUTHENTICATED  0x10U
#define XGL_WIRE_FLAG_CONTROL        0x20U

typedef enum {
    XGL_WIRE_EXT_SESSION = 1,
    XGL_WIRE_EXT_ACK_RANGE = 2,
    XGL_WIRE_EXT_SACK = 3,
    XGL_WIRE_EXT_FRAGMENT = 4,
    XGL_WIRE_EXT_SECURITY = 5,
    XGL_WIRE_EXT_ROUTE = 6,
    XGL_WIRE_EXT_TIMESTAMP = 7,
    XGL_WIRE_EXT_DATA_TYPE = 8
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

/**
 * \brief           ACK fields and ranges borrowing their encoded value bytes
 * \note            The source bytes must remain alive and unchanged while the
 *                  view is used. Sequence semantics belong to transport.
 */
typedef struct {
    uint32_t largest_ack;
    uint32_t ack_delay_us;
    const uint8_t* ranges;
    size_t range_count;
} xgl_wire_ack_range_view_t;

typedef struct {
    const uint8_t* buffer;
    size_t len;
    size_t offset;
} xgl_wire_ext_cursor_t;

/**
 * \brief           Decoded singleton extensions shared by protocol layers
 */
typedef struct {
    uint8_t data_type;
    bool data_type_found;
    uint32_t session_epoch;
    bool session_epoch_found;
    uint64_t incarnation_id;
    uint32_t auth_key_id;
    uint64_t nonce_id;
    uint8_t auth_tag_len;
    bool has_security_ext;
} xgl_wire_ext_metadata_t;

/**
 * \brief           Validated frame view borrowing the original input bytes
 * \note            The input buffer must remain valid and unchanged while this
 *                  view is used. Decoding validates layout and CRC only;
 *                  authenticated indicates the wire flag, not a verified tag.
 */
typedef struct {
    xgl_wire_header_t header;
    uint8_t data_type;
    uint32_t session_epoch;
    const uint8_t* extensions;
    size_t extensions_len;
    const uint8_t* payload;
    size_t payload_len;
    uint8_t reliable;
    uint8_t fragment;
    uint8_t priority;
    const uint8_t* frame_buf;
    size_t frame_len;
    uint32_t auth_key_id;
    uint64_t nonce_id;
    uint64_t incarnation_id;
    uint8_t auth_tag_len;
    bool has_security_ext;
    bool authenticated;
} xgl_wire_frame_view_t;

typedef enum {
    XGL_WIRE_DECODE_INVALID = 0,
    XGL_WIRE_DECODE_OK,
    XGL_WIRE_DECODE_HEADER_CRC,
    XGL_WIRE_DECODE_FRAME_CRC
} xgl_wire_decode_status_t;

/**
 * \brief           Decode bounded TLVs and reject duplicate singleton
 *                  extensions
 * \param[in]       extensions: Extension bytes, or NULL for an empty span
 * \param[in]       extensions_len: Number of available extension bytes
 * \param[out]      metadata: Decoded metadata, valid only on success
 * \return          XGL_OK on success, error code otherwise
 * \note            Unknown extension types are skipped for wire v3
 *                  compatibility.
 */
xgl_error_t xgl_wire_decode_ext_metadata(const uint8_t* extensions,
                                         size_t extensions_len,
                                         xgl_wire_ext_metadata_t* metadata);

/**
 * \brief           Validate a complete frame and expose borrowed byte spans
 * \param[out]      view: Decoded frame view, cleared on failure
 * \param[in]       buffer: Complete frame bytes, including the final CRC
 * \param[in]       frame_len: Number of available frame bytes
 * \param[out]      status: Optional validation stage for error statistics
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_wire_decode_frame(xgl_wire_frame_view_t* view,
                                  const uint8_t* buffer, size_t frame_len,
                                  xgl_wire_decode_status_t* status);

/*---------------------------------------------------------------------------*/
/* Wire Encoding API                                                          */
/*---------------------------------------------------------------------------*/

xgl_error_t xgl_wire_encode_header(uint8_t* buffer, size_t buffer_size,
                                   const xgl_wire_header_t* header);

xgl_error_t xgl_wire_decode_header(xgl_wire_header_t* header,
                                   const uint8_t* buffer, size_t buffer_size);

xgl_error_t xgl_wire_encode_ext(uint8_t* buffer, size_t buffer_size,
                                uint8_t type, const uint8_t* value,
                                size_t value_len, size_t* bytes_written);

xgl_error_t xgl_wire_ext_cursor_init(xgl_wire_ext_cursor_t* cursor,
                                     const uint8_t* buffer, size_t len);

xgl_error_t xgl_wire_ext_cursor_next(xgl_wire_ext_cursor_t* cursor,
                                     xgl_wire_ext_t* ext);

xgl_error_t
xgl_wire_encode_ack_range_ext_value(uint8_t* buffer, size_t buffer_size,
                                    uint32_t largest_ack, uint32_t ack_delay_us,
                                    const xgl_wire_ack_range_t* ranges,
                                    size_t range_count, size_t* bytes_written);

xgl_error_t xgl_wire_decode_ack_range_ext_value(
    const uint8_t* buffer, size_t buffer_size, uint32_t* largest_ack,
    uint32_t* ack_delay_us, xgl_wire_ack_range_t* ranges, size_t range_capacity,
    size_t* range_count);

/** \brief           Validate an ACK value and borrow its range bytes. */
xgl_error_t xgl_wire_decode_ack_range_view(const uint8_t* buffer,
                                           size_t buffer_size,
                                           xgl_wire_ack_range_view_t* view);

/** \brief           Read one range, or return NOT_FOUND past the last range. */
xgl_error_t xgl_wire_ack_range_at(const xgl_wire_ack_range_view_t* view,
                                  size_t index, xgl_wire_ack_range_t* range);

xgl_error_t xgl_wire_encode_sack_ext_value(uint8_t* buffer, size_t buffer_size,
                                           uint32_t base_packet,
                                           const uint8_t* bitmap,
                                           size_t bitmap_len,
                                           size_t* bytes_written);

xgl_error_t
xgl_wire_decode_sack_ext_value(const uint8_t* buffer, size_t buffer_size,
                               uint32_t* base_packet, uint8_t* bitmap,
                               size_t bitmap_capacity, size_t* bitmap_len);

xgl_error_t xgl_wire_encode_fragment_ext_value(
    uint8_t* buffer, size_t buffer_size, uint32_t message_id,
    uint32_t fragment_offset, uint32_t message_len, size_t* bytes_written);

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

xgl_error_t
xgl_wire_encode_security_ext_value(uint8_t* buffer, size_t buffer_size,
                                   uint32_t key_id, uint64_t nonce_id,
                                   uint8_t tag_len, size_t* bytes_written);

xgl_error_t xgl_wire_decode_security_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* key_id,
                                               uint64_t* nonce_id,
                                               uint8_t* tag_len);

xgl_error_t xgl_wire_encode_route_ext_value(uint8_t* buffer, size_t buffer_size,
                                            uint16_t previous_hop,
                                            uint16_t next_hop,
                                            uint32_t route_epoch,
                                            uint16_t metric,
                                            size_t* bytes_written);

xgl_error_t
xgl_wire_decode_route_ext_value(const uint8_t* buffer, size_t buffer_size,
                                uint16_t* previous_hop, uint16_t* next_hop,
                                uint32_t* route_epoch, uint16_t* metric);

#ifdef __cplusplus
}
#endif

#endif /* XGL_WIRE_H */
