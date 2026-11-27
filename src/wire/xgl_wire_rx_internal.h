/**
 * \file            xgl_wire_rx_internal.h
 * \brief           Shared receive validation for complete and streaming frames
 * \author          X-Gen Lab
 */

#ifndef XGL_WIRE_RX_INTERNAL_H
#define XGL_WIRE_RX_INTERNAL_H

#include "wire/xgl_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief           TTL byte in the fixed Wire v1 header. */
#define XGL_WIRE_TTL_OFFSET 6U

/**
 * \brief           Decrement the TTL and rewrite both CRCs
 * \param[in,out]   buffer: Mutable bytes of the exact validated frame
 * \param[in]       frame_len: Complete length including the frame CRC
 * \note            Pass a validated frame whose TTL is greater than one.
 */
void xgl_wire_decrement_frame_ttl(uint8_t* buffer, size_t frame_len);

/**
 * \brief           Compact layout of one candidate with validated TLVs
 * \note            Offsets are relative to the start of the TLV span and
 *                  remain valid after moving the entire candidate. Zero
 *                  denotes an absent extension.
 */
typedef struct {
    uint8_t data_type_offset; /**< DATA_TYPE value location */
    uint8_t session_offset;   /**< SESSION value location */
    uint8_t security_offset;  /**< SECURITY value location */
    uint8_t auth_tag_len;     /**< Tag length, excluding the frame CRC */
} xgl_wire_rx_layout_t;

/**
 * \brief           Verify and decode the entire fixed header
 * \param[out]      header: Decoded fields, valid on success
 * \param[in]       buffer: Candidate frame prefix
 * \param[in]       available: Available prefix length
 * \param[out]      status: Optional diagnostic updated on a header CRC error
 * \return          XGL_OK or the header decode error
 */
xgl_error_t xgl_wire_rx_check_header(xgl_wire_header_t* header,
                                     const uint8_t* buffer, size_t available,
                                     xgl_wire_decode_status_t* status);

/**
 * \brief           Scan TLVs once and record accepted singleton locations
 * \param[out]      layout: Accepted extension layout, written on success
 * \param[in]       buffer: Candidate frame prefix
 * \param[in]       header_len: Complete header size from the validated header
 * \return          XGL_OK or the extension decode error
 * \note            The caller has validated the fixed header. Its header_len
 *                  bytes must all be available.
 */
xgl_error_t xgl_wire_rx_check_extensions(xgl_wire_rx_layout_t* layout,
                                         const uint8_t* buffer,
                                         uint8_t header_len);

/**
 * \brief           Validate complete frame size and CRC and expose a view
 * \param[out]      view: Borrowed frame view, written only on success
 * \param[in]       buffer: Candidate frame bytes, immutable while view is used
 * \param[in]       frame_len: Complete length including the frame CRC
 * \param[in]       layout: Validated extension offsets for this candidate
 * \param[out]      status: Optional diagnostic set on CRC failure or success
 * \return          XGL_OK or a length or CRC error
 * \note            Pass the same header and TLV bytes used to collect layout.
 */
xgl_error_t xgl_wire_rx_complete(xgl_wire_frame_view_t* view,
                                 const uint8_t* buffer, size_t frame_len,
                                 const xgl_wire_rx_layout_t* layout,
                                 xgl_wire_decode_status_t* status);

#ifdef __cplusplus
}
#endif

#endif /* XGL_WIRE_RX_INTERNAL_H */
