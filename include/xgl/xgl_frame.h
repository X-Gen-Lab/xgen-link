/**
 * \file            xgl_frame.h
 * \brief           Frame encapsulation and structure definitions
 * \author          Nexus Team
 */

#ifndef XGL_FRAME_H
#define XGL_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_types.h"
#include "xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Frame Structure                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Complete frame structure
 * \details         Represents a complete protocol frame with all components
 * \note            The header already contains SOF, so no separate SOF field
 */
typedef struct {
    xgl_frame_header_t header;      /**< Frame header (12 bytes with SOF) */
    const uint8_t* payload;         /**< Pointer to payload data */
    size_t payload_len;             /**< Payload length in bytes */
    uint16_t crc16;                 /**< Frame CRC16 */
} xgl_frame_t;

/*---------------------------------------------------------------------------*/
/* Frame Building Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Frame building parameters
 * \details         Encapsulates all parameters needed to build a frame
 */
typedef struct {
    uint8_t source_id;          /**< Source node ID */
    uint8_t target_id;          /**< Target node ID */
    uint8_t data_type;          /**< Data type */
    uint8_t seq_num;            /**< Sequence number */
    uint8_t ack_num;            /**< Acknowledgment number */
    const uint8_t* payload;     /**< Payload data */
    size_t payload_len;         /**< Payload length */
    bool reliable;              /**< Reliable transmission flag */
    uint8_t reliable_type;      /**< Raw reliable attribute type, 0 uses reliable flag */
    bool fragment;              /**< Fragment flag */
    uint8_t priority;           /**< Priority level (0-7) */
    uint16_t session_id;        /**< Transport epoch/session ID, encoded in attr_msb low bits */
    uint8_t ttl;                /**< Hop limit stored in reserved header byte */
} xgl_frame_params_t;

/**
 * \brief           Build frame from parameters
 * \param[out]      frame: Frame structure to populate
 * \param[in]       params: Frame building parameters
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_build(xgl_frame_t* frame,
                            const xgl_frame_params_t* params);

/**
 * \brief           Serialize frame to buffer
 * \param[out]      buffer: Output buffer
 * \param[in]       buffer_size: Buffer size in bytes
 * \param[in]       frame: Frame structure to serialize
 * \param[out]      bytes_written: Number of bytes written
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_serialize(uint8_t* buffer,
                                size_t buffer_size,
                                const xgl_frame_t* frame,
                                size_t* bytes_written);

/**
 * \brief           Build frame in zero-copy mode
 * \param[in,out]   buffer: Buffer with pre-allocated header space
 * \param[in]       buffer_size: Total buffer size
 * \param[in]       data_offset: Offset where payload data starts
 * \param[in]       data_len: Payload data length
 * \param[in]       source_id: Source node ID
 * \param[in]       target_id: Target node ID
 * \param[in]       data_type: Data type
 * \param[in]       seq_num: Sequence number
 * \param[in]       ack_num: Acknowledgment number
 * \param[in]       reliable: Reliable transmission flag
 * \param[in]       priority: Priority level (0-7)
 * \param[out]      frame_len: Total frame length
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_build_zerocopy(uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t data_offset,
                                     size_t data_len,
                                     uint8_t source_id,
                                     uint8_t target_id,
                                     uint8_t data_type,
                                     uint8_t seq_num,
                                     uint8_t ack_num,
                                     bool reliable,
                                     uint8_t priority,
                                     size_t* frame_len);

/**
 * \brief           Encode frame header to buffer
 * \param[out]      buffer: Output buffer (must be at least 12 bytes)
 * \param[in]       header: Frame header structure
 */
void xgl_frame_encode_header(uint8_t* buffer, const xgl_frame_header_t* header);

/**
 * \brief           Decode frame header from buffer
 * \param[out]      header: Frame header structure to populate
 * \param[in]       buffer: Input buffer (must be at least 12 bytes)
 */
void xgl_frame_decode_header(xgl_frame_header_t* header, const uint8_t* buffer);

/*---------------------------------------------------------------------------*/
/* Frame Validation Functions                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Validate frame header CRC8
 * \param[in]       header: Frame header structure
 * \return          true if CRC8 is valid, false otherwise
 */
bool xgl_frame_validate_header_crc(const xgl_frame_header_t* header);

/**
 * \brief           Calculate frame size
 * \param[in]       payload_len: Payload length in bytes
 * \return          Total frame size (header with SOF + payload + CRC16)
 */
static inline size_t xgl_frame_calculate_size(size_t payload_len) {
    return XGL_FRAME_HEADER_SIZE + payload_len + XGL_CRC16_SIZE;
}

/*---------------------------------------------------------------------------*/
/* Attribute Helper Functions                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Set version in header
 * \param[in,out]   header: Frame header
 * \param[in]       version: Protocol version (0-15)
 */
static inline void xgl_frame_set_version(xgl_frame_header_t* header, uint8_t version) {
    uint32_t version_field = ((uint32_t)version & 0x0FU) << 4;
    header->version_datatype = (uint8_t)(((uint32_t)header->version_datatype & 0x0FU) |
                                         version_field);
}

/**
 * \brief           Get version from header
 * \param[in]       header: Frame header
 * \return          Protocol version (0-15)
 */
static inline uint8_t xgl_frame_get_version(const xgl_frame_header_t* header) {
    return (header->version_datatype >> 4) & 0x0F;
}

/**
 * \brief           Set data type in header
 * \param[in,out]   header: Frame header
 * \param[in]       data_type: Data type (0-15)
 */
static inline void xgl_frame_set_datatype(xgl_frame_header_t* header, uint8_t data_type) {
    header->version_datatype = (uint8_t)(((uint32_t)header->version_datatype & 0xF0U) |
                                         ((uint32_t)data_type & 0x0FU));
}

/**
 * \brief           Get data type from header
 * \param[in]       header: Frame header
 * \return          Data type (0-15)
 */
static inline uint8_t xgl_frame_get_datatype(const xgl_frame_header_t* header) {
    return (uint8_t)(header->version_datatype & 0x0FU);
}

/**
 * \brief           Set reliable attribute in attributes LSB
 * \param[in,out]   attr_lsb: Attributes LSB byte
 * \param[in]       reliable: Reliable transmission flag
 */
static inline void xgl_frame_set_reliable(uint8_t* attr_lsb, bool reliable) {
    uint8_t reliable_bits = reliable ? XGL_ATTR_RELIABLE_TX : XGL_ATTR_RELIABLE_NONE;
    *attr_lsb = (uint8_t)(((uint32_t)*attr_lsb & ~((uint32_t)XGL_ATTR_RELIABLE_MASK)) |
                          (uint32_t)reliable_bits);
}

/**
 * \brief           Set reliable attribute type in attributes LSB
 * \param[in,out]   attr_lsb: Attributes LSB byte
 * \param[in]       reliable_type: Raw reliable type bits
 */
static inline void xgl_frame_set_reliable_type(uint8_t* attr_lsb, uint8_t reliable_type) {
    *attr_lsb = (uint8_t)(((uint32_t)*attr_lsb & ~((uint32_t)XGL_ATTR_RELIABLE_MASK)) |
                          ((uint32_t)reliable_type & (uint32_t)XGL_ATTR_RELIABLE_MASK));
}

/**
 * \brief           Set fragment attribute in attributes LSB
 * \param[in,out]   attr_lsb: Attributes LSB byte
 * \param[in]       fragment: Fragment flag
 */
static inline void xgl_frame_set_fragment(uint8_t* attr_lsb, bool fragment) {
    if (fragment) {
        *attr_lsb = (uint8_t)((uint32_t)*attr_lsb | (uint32_t)XGL_ATTR_FRAGMENT_MASK);
    } else {
        *attr_lsb = (uint8_t)((uint32_t)*attr_lsb & ~((uint32_t)XGL_ATTR_FRAGMENT_MASK));
    }
}

/**
 * \brief           Set priority attribute in attributes LSB
 * \param[in,out]   attr_lsb: Attributes LSB byte
 * \param[in]       priority: Priority level (0-7)
 */
static inline void xgl_frame_set_priority(uint8_t* attr_lsb, uint8_t priority) {
    *attr_lsb = (uint8_t)(((uint32_t)*attr_lsb & ~((uint32_t)XGL_ATTR_PRIORITY_MASK)) |
                          ((uint32_t)priority & (uint32_t)XGL_ATTR_PRIORITY_MASK));
}

/**
 * \brief           Get reliable attribute from attributes LSB
 * \param[in]       attr_lsb: Attributes LSB byte
 * \return          Reliable transmission type
 */
static inline uint8_t xgl_frame_get_reliable(uint8_t attr_lsb) {
    return (uint8_t)(((uint32_t)attr_lsb & (uint32_t)XGL_ATTR_RELIABLE_MASK) >>
                     XGL_ATTR_RELIABLE_SHIFT);
}

/**
 * \brief           Get priority attribute from attributes LSB
 * \param[in]       attr_lsb: Attributes LSB byte
 * \return          Priority level (0-7)
 */
static inline uint8_t xgl_frame_get_priority(uint8_t attr_lsb) {
    return (uint8_t)(attr_lsb & XGL_ATTR_PRIORITY_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_FRAME_H */
