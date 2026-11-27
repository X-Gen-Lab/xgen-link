/**
 * \file            xgl_wire.h
 * \brief           Wire v1 header and extension codecs
 * \author          X-Gen Lab
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

#define XGL_WIRE_MAGIC_0             ((uint8_t)0xA5U)
#define XGL_WIRE_MAGIC_1             ((uint8_t)0x5AU)
#define XGL_WIRE_VERSION             1U
#define XGL_WIRE_BASE_HEADER_SIZE    XGL_FRAME_HEADER_SIZE
#define XGL_WIRE_EXT_HEADER_SIZE     2U
#define XGL_WIRE_MAX_EXTENSIONS_SIZE (UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE)
#define XGL_SESSION_EXT_VALUE_SIZE   12U
#define XGL_SESSION_EXT_SIZE                                                   \
    (XGL_WIRE_EXT_HEADER_SIZE + XGL_SESSION_EXT_VALUE_SIZE)
#define XGL_SECURITY_EXT_VALUE_SIZE 13U
#define XGL_SECURITY_EXT_SIZE                                                  \
    (XGL_WIRE_EXT_HEADER_SIZE + XGL_SECURITY_EXT_VALUE_SIZE)

/** \brief           Size in bytes of the optional DATA_TYPE and SESSION TLVs.
 */
static inline size_t xgl_wire_packet_extension_size(uint8_t data_type,
                                                    uint32_t session_epoch) {
    return (data_type != 0U ? XGL_DATA_TYPE_EXT_SIZE : 0U) +
           (session_epoch != 0U ? XGL_SESSION_EXT_SIZE : 0U);
}

typedef enum {
    XGL_PACKET_TYPE_INVALID = 0,
    XGL_PACKET_TYPE_DATA = 1,
    XGL_PACKET_TYPE_ACK = 2,
    XGL_PACKET_TYPE_CONTROL = 3
} xgl_packet_type_t;

#define XGL_WIRE_FLAG_ACK_ELICITING  0x01U
#define XGL_WIRE_FLAG_HAS_EXTENSIONS 0x02U
#define XGL_WIRE_FLAG_AUTHENTICATED  0x04U
#define XGL_WIRE_FLAG_CONTROL        0x08U

typedef enum {
    XGL_WIRE_EXT_SESSION = 1,
    XGL_WIRE_EXT_ACK_RANGE = 2,
    XGL_WIRE_EXT_SACK = 3,
    XGL_WIRE_EXT_SECURITY = 4,
    XGL_WIRE_EXT_DATA_TYPE = 5
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

/**
 * \brief           SACK fields borrowing the encoded bitmap bytes
 * \note            Source bytes must remain alive and unchanged while the view
 *                  is used. Transport applies its own receive capacity and
 *                  packet-number validation before changing peer state.
 */
typedef struct {
    const uint8_t* bitmap;
    size_t bitmap_len;
    uint32_t base_packet;
} xgl_wire_sack_view_t;

typedef struct {
    const uint8_t* buffer;
    size_t len;
    size_t offset;
} xgl_wire_ext_cursor_t;

/**
 * \brief           Decoded singleton extensions shared by protocol layers
 */
typedef struct {
    uint32_t session_epoch;
    uint8_t data_type;
    uint8_t auth_tag_len;
    uint8_t data_type_offset; /**< Value offset in the TLV span, or 0 */
    uint8_t session_offset;   /**< Value offset in the TLV span, or 0 */
    uint8_t security_offset;  /**< Value offset in the TLV span, or 0 */
} xgl_wire_ext_metadata_t;

/**
 * \brief           Validated frame view borrowing the original input bytes
 * \note            Keep the input bytes alive and unchanged while using the
 *                  view. Wire validates header fields, layout and both CRCs.
 *                  Security verifies the authentication tag separately.
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
 * \param[in]       authenticated: Whether the span must contain SECURITY
 * \param[out]      metadata: Decoded metadata, valid only on success
 * \return          XGL_OK on success, error code otherwise
 * \note            Unassigned extension types are skipped.
 */
xgl_error_t xgl_wire_decode_ext_metadata(const uint8_t* extensions,
                                         size_t extensions_len,
                                         bool authenticated,
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

/**
 * \brief           Write the fixed header in wire byte order
 * \param[out]      buffer: Destination for the 24-byte fixed header
 * \param[in]       buffer_size: Destination capacity in bytes
 * \param[in]       header: Header fields; the encoder calculates header_crc16
 * \return          XGL_OK on success, error code otherwise
 * \note            Extension and payload bytes are written by the caller.
 */
xgl_error_t xgl_wire_encode_header(uint8_t* buffer, size_t buffer_size,
                                   const xgl_wire_header_t* header);

/**
 * \brief           Check fixed header fields and the header CRC
 * \param[out]      header: Decoded fields, valid on success
 * \param[in]       buffer: Bytes beginning with the wire magic
 * \param[in]       buffer_size: Number of available bytes
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_wire_decode_header(xgl_wire_header_t* header,
                                   const uint8_t* buffer, size_t buffer_size);

/**
 * \brief           Write a complete type-length-value extension
 * \param[out]      buffer: Destination disjoint from the value bytes
 * \param[in]       buffer_size: Destination capacity in bytes
 * \param[in]       type: Nonzero Wire v1 extension code
 * \param[in]       value: Value bytes, or NULL for an empty value
 * \param[in]       value_len: Number of value bytes, at most UINT8_MAX
 * \param[out]      bytes_written: Total TLV size, set on success
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_wire_encode_ext(uint8_t* buffer, size_t buffer_size,
                                uint8_t type, const uint8_t* value,
                                size_t value_len, size_t* bytes_written);

/**
 * \brief           Initialize a cursor over the exact extension span
 * \param[out]      cursor: Cursor to advance with xgl_wire_ext_cursor_next()
 * \param[in]       buffer: Borrowed extension bytes, or NULL if len is zero
 * \param[in]       len: Number of extension bytes
 * \return          XGL_OK on success, error code otherwise
 * \note            Keep the bytes unchanged while using the cursor or a TLV.
 */
xgl_error_t xgl_wire_ext_cursor_init(xgl_wire_ext_cursor_t* cursor,
                                     const uint8_t* buffer, size_t len);

/**
 * \brief           Advance an initialized cursor by one complete TLV
 * \param[in,out]   cursor: Cursor over the available extension bytes
 * \param[out]      ext: Borrowed TLV, cleared at end or on a decode error
 * \return          XGL_OK, XGL_ERR_NOT_FOUND at the end or a decode error
 */
xgl_error_t xgl_wire_ext_cursor_next(xgl_wire_ext_cursor_t* cursor,
                                     xgl_wire_ext_t* ext);

/**
 * \brief           Encode ACK_RANGE value bytes, excluding the TLV header
 * \param[out]      buffer: Destination separate from the range array
 * \param[in]       buffer_size: Destination capacity in bytes
 * \param[in]       largest_ack: Largest acknowledged packet number
 * \param[in]       ack_delay_us: Acknowledgment delay in microseconds
 * \param[in]       ranges: Range array, or NULL when range_count is zero
 * \param[in]       range_count: Number of entries in ranges
 * \param[out]      bytes_written: Encoded value length, set on success
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t
xgl_wire_encode_ack_range_ext_value(uint8_t* buffer, size_t buffer_size,
                                    uint32_t largest_ack, uint32_t ack_delay_us,
                                    const xgl_wire_ack_range_t* ranges,
                                    size_t range_count, size_t* bytes_written);

/**
 * \brief           Validate an ACK_RANGE value and borrow its encoded ranges
 * \param[in]       buffer: Borrowed value bytes, excluding the TLV header
 * \param[in]       buffer_size: Exact value length in bytes
 * \param[out]      view: Fields and borrowed bytes, cleared on failure
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_wire_decode_ack_range_view(const uint8_t* buffer,
                                           size_t buffer_size,
                                           xgl_wire_ack_range_view_t* view);

/** \brief           Read one range, or return NOT_FOUND past the last range. */
xgl_error_t xgl_wire_ack_range_at(const xgl_wire_ack_range_view_t* view,
                                  size_t index, xgl_wire_ack_range_t* range);

/**
 * \brief           Encode SACK value bytes, excluding the TLV header
 * \param[out]      buffer: Destination separate from the bitmap bytes
 * \param[in]       buffer_size: Destination capacity in bytes
 * \param[in]       base_packet: Base packet number for the bitmap
 * \param[in]       bitmap: Bitmap bytes, or NULL when bitmap_len is zero
 * \param[in]       bitmap_len: Number of bytes in bitmap
 * \param[out]      bytes_written: Encoded value length, set on success
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_wire_encode_sack_ext_value(uint8_t* buffer, size_t buffer_size,
                                           uint32_t base_packet,
                                           const uint8_t* bitmap,
                                           size_t bitmap_len,
                                           size_t* bytes_written);

/**
 * \brief           Validate a complete SACK value without copying its bitmap
 * \param[in]       buffer: Borrowed value bytes, excluding the TLV header
 * \param[in]       buffer_size: Exact encoded value length
 * \param[out]      view: Fields and borrowed bytes, cleared on failure
 * \return          XGL_OK, NULL_POINTER or INVALID_FRAME
 * \note            Empty bitmaps are structurally valid; transport determines
 *                  whether the value can acknowledge a packet.
 */
xgl_error_t xgl_wire_decode_sack_view(const uint8_t* buffer, size_t buffer_size,
                                      xgl_wire_sack_view_t* view);

/** \brief           Write the 12-byte SESSION value in wire byte order. */
xgl_error_t xgl_wire_encode_session_ext_value(uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t session_epoch,
                                              uint64_t incarnation_id,
                                              size_t* bytes_written);

/** \brief           Read epoch and incarnation from an exact SESSION value. */
xgl_error_t xgl_wire_decode_session_ext_value(const uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t* session_epoch,
                                              uint64_t* incarnation_id);

/** \brief           Write SECURITY with a nonzero tag length in bytes. */
xgl_error_t
xgl_wire_encode_security_ext_value(uint8_t* buffer, size_t buffer_size,
                                   uint32_t key_id, uint64_t nonce_id,
                                   uint8_t tag_len, size_t* bytes_written);

/** \brief           Read key, nonce and tag length from a SECURITY value. */
xgl_error_t xgl_wire_decode_security_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* key_id,
                                               uint64_t* nonce_id,
                                               uint8_t* tag_len);

#ifdef __cplusplus
}
#endif

#endif /* XGL_WIRE_H */
