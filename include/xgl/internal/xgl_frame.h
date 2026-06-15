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
#include "xgl/xgl_types.h"
#include "xgl/xgl_error.h"
#include "xgl/internal/xgl_wire.h"

/*---------------------------------------------------------------------------*/
/* Frame Structure                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Complete frame structure
 * \details         Represents a complete protocol frame with all components
 * \note            The fixed header already contains the production magic bytes
 */
typedef struct {
    xgl_wire_header_t header;       /**< Production logical wire header */
    const uint8_t* extensions;      /**< Production TLV extension bytes */
    size_t extensions_len;          /**< Extension length in bytes */
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
    uint16_t source_id;         /**< Source node ID */
    uint16_t target_id;         /**< Target node ID */
    uint8_t data_type;          /**< Application/control data type; encoded by upper layers */
    uint8_t packet_type;        /**< Production packet type, 0 uses DATA */
    uint8_t flags;              /**< Production wire flags merged with derived flags */
    uint8_t traffic_class;      /**< Production traffic class, 0 uses priority */
    uint32_t connection_id;     /**< Production connection context ID */
    uint32_t packet_number;     /**< Production monotonic packet number */
    uint32_t session_epoch;     /**< Production session epoch for SESSION_EXT users */
    const uint8_t* extensions;   /**< Production TLV extension bytes */
    size_t extensions_len;       /**< Extension length in bytes */
    const uint8_t* payload;     /**< Payload data */
    size_t payload_len;         /**< Payload length */
    bool reliable;              /**< Reliable transmission flag */
    uint8_t reliability_class;  /**< Raw reliability class, 0 uses reliable flag */
    bool fragment;              /**< Fragment flag */
    uint8_t priority;           /**< Priority level (0-7) */
    uint16_t session_id;        /**< Short transport session ID fallback for connection_id */
    uint8_t ttl;                /**< Hop limit */
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
 * \brief           Serialize frame with production authentication trailer
 * \param[out]      buffer: Output buffer
 * \param[in]       buffer_size: Buffer size in bytes
 * \param[in]       frame: Frame structure to serialize
 * \param[in]       key_id: Authentication key identifier
 * \param[in]       provider: Authentication provider callbacks
 * \param[out]      bytes_written: Number of bytes written
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_serialize_authenticated(uint8_t* buffer,
                                              size_t buffer_size,
                                              const xgl_frame_t* frame,
                                              uint32_t key_id,
                                              const xgl_auth_provider_t* provider,
                                              size_t* bytes_written);

/**
 * \brief           Build frame in zero-copy mode
 * \param[in,out]   buffer: Buffer with pre-allocated header space
 * \param[in]       buffer_size: Total buffer size
 * \param[in]       data_offset: Payload offset; use XGL_FRAME_HEADER_SIZE plus
 *                  XGL_DATA_TYPE_EXT_SIZE when data_type is non-zero
 * \param[in]       data_len: Payload data length
 * \param[in]       source_id: Source node ID
 * \param[in]       target_id: Target node ID
 * \param[in]       data_type: Application data type encoded as DATA_TYPE_EXT
 * \param[in]       packet_number: Production packet number
 * \param[in]       reliable: Reliable transmission flag
 * \param[in]       priority: Priority level (0-7)
 * \param[out]      frame_len: Total frame length
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_build_zerocopy(uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t data_offset,
                                     size_t data_len,
                                     uint16_t source_id,
                                     uint16_t target_id,
                                     uint8_t data_type,
                                     uint32_t packet_number,
                                     bool reliable,
                                     uint8_t priority,
                                     size_t* frame_len);

/**
 * \brief           Calculate frame size
 * \param[in]       payload_len: Payload length in bytes
 * \return          Total frame size (fixed header + payload + CRC16)
 */
static inline size_t xgl_frame_calculate_size(size_t payload_len) {
    return XGL_FRAME_HEADER_SIZE + payload_len + XGL_CRC16_SIZE;
}

/*---------------------------------------------------------------------------*/
/* Traffic-Class Helper Functions                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Set reliability class in traffic-class bits
 * \param[in,out]   traffic_class: Traffic-class byte
 * \param[in]       reliable: Reliable transmission flag
 */
static inline void xgl_frame_set_reliability(uint8_t* traffic_class, bool reliable) {
    uint8_t reliable_bits = reliable ? XGL_RELIABILITY_ACK_ELICITING : XGL_RELIABILITY_NONE;
    *traffic_class = (uint8_t)(((uint32_t)*traffic_class &
                                ~((uint32_t)XGL_RELIABILITY_CLASS_MASK)) |
                               (uint32_t)reliable_bits);
}

/**
 * \brief           Set raw reliability class in traffic-class bits
 * \param[in,out]   traffic_class: Traffic-class byte
 * \param[in]       reliability_class: Raw reliability class bits
 */
static inline void xgl_frame_set_reliability_class(uint8_t* traffic_class,
                                                   uint8_t reliability_class) {
    *traffic_class = (uint8_t)(((uint32_t)*traffic_class &
                                ~((uint32_t)XGL_RELIABILITY_CLASS_MASK)) |
                               ((uint32_t)reliability_class &
                                (uint32_t)XGL_RELIABILITY_CLASS_MASK));
}

/**
 * \brief           Set fragmented bit in traffic-class bits
 * \param[in,out]   traffic_class: Traffic-class byte
 * \param[in]       fragment: Fragment flag
 */
static inline void xgl_frame_set_fragmented(uint8_t* traffic_class, bool fragment) {
    if (fragment) {
        *traffic_class = (uint8_t)((uint32_t)*traffic_class |
                                   (uint32_t)XGL_TRAFFIC_FRAGMENTED_MASK);
    } else {
        *traffic_class = (uint8_t)((uint32_t)*traffic_class &
                                   ~((uint32_t)XGL_TRAFFIC_FRAGMENTED_MASK));
    }
}

/**
 * \brief           Set priority in traffic-class bits
 * \param[in,out]   traffic_class: Traffic-class byte
 * \param[in]       priority: Priority level (0-7)
 */
static inline void xgl_frame_set_priority(uint8_t* traffic_class, uint8_t priority) {
    *traffic_class = (uint8_t)(((uint32_t)*traffic_class &
                                ~((uint32_t)XGL_TRAFFIC_PRIORITY_MASK)) |
                               ((uint32_t)priority & (uint32_t)XGL_TRAFFIC_PRIORITY_MASK));
}

/**
 * \brief           Get reliability class from traffic-class bits
 * \param[in]       traffic_class: Traffic-class byte
 * \return          Reliable transmission type
 */
static inline uint8_t xgl_frame_get_reliability(uint8_t traffic_class) {
    return (uint8_t)(((uint32_t)traffic_class &
                      (uint32_t)XGL_RELIABILITY_CLASS_MASK) >>
                     XGL_RELIABILITY_CLASS_SHIFT);
}

/**
 * \brief           Get priority from traffic-class bits
 * \param[in]       traffic_class: Traffic-class byte
 * \return          Priority level (0-7)
 */
static inline uint8_t xgl_frame_get_priority(uint8_t traffic_class) {
    return (uint8_t)(traffic_class & XGL_TRAFFIC_PRIORITY_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_FRAME_H */
