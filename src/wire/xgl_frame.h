/**
 * \file            xgl_frame.h
 * \brief           Frame encapsulation and structure definitions
 * \author          X-Gen Lab
 */

#ifndef XGL_FRAME_H
#define XGL_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wire/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Frame Structure                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Logical frame and borrowed extension and payload bytes
 * \note            Keep both spans alive until encoding returns. The encoder
 *                  writes the fixed header CRC. Finalization writes the CRC
 *                  that covers the complete encoded frame body.
 */
typedef struct {
    xgl_wire_header_t header;  /**< Production logical wire header */
    const uint8_t* extensions; /**< Production TLV extension bytes */
    size_t extensions_len;     /**< Extension length in bytes */
    const uint8_t* payload;    /**< Pointer to payload data */
    size_t payload_len;        /**< Payload length in bytes */
} xgl_frame_t;

/*---------------------------------------------------------------------------*/
/* Frame Building Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Frame building parameters
 * \note            DATA_TYPE and SESSION are supplied in the extension span.
 */
typedef struct {
    uint16_t source_id;  /**< Source node ID */
    uint16_t target_id;  /**< Target node ID */
    uint8_t packet_type; /**< Packet type, 0 derives DATA or ACK from class */
    uint8_t flags;       /**< AUTHENTICATED requests an authenticated layout */
    uint32_t connection_id;    /**< Production connection context ID */
    uint32_t packet_number;    /**< Production monotonic packet number */
    const uint8_t* extensions; /**< Production TLV extension bytes */
    size_t extensions_len;     /**< Extension length in bytes */
    const uint8_t* payload;    /**< Payload data */
    size_t payload_len;        /**< Payload length */
    bool reliable;             /**< Reliable transmission flag */
    uint8_t
        reliability_class; /**< Raw reliability class, 0 uses reliable flag */
    uint8_t priority;      /**< Priority level (0-7) */
    uint8_t ttl;           /**< Hop limit */
} xgl_frame_params_t;

/**
 * \brief           Set logical header fields and borrow the source spans
 * \param[out]      frame: Logical frame, written only on success
 * \param[in]       params: Frame building parameters
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_build(xgl_frame_t* frame,
                            const xgl_frame_params_t* params);

/**
 * \brief           Serialize a plaintext frame into caller storage
 * \param[out]      buffer: Output buffer
 * \param[in]       buffer_size: Buffer size in bytes
 * \param[in]       frame: Frame and source spans held until the call returns
 * \param[out]      bytes_written: Complete frame size, zero on failure
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_serialize(uint8_t* buffer, size_t buffer_size,
                                const xgl_frame_t* frame,
                                size_t* bytes_written);

/** \brief           Checked sizes and scope for one encoded frame. */
typedef struct {
    size_t header_len;
    size_t payload_len;
    size_t tag_len;
    size_t frame_len;
    uint32_t session_epoch; /**< Checked SESSION epoch, or 0 if unavailable */
} xgl_frame_layout_t;

/**
 * \brief           Measure bounded frame lengths without reading payload bytes
 * \param[in]       payload_len: Number of payload bytes
 * \param[in]       extensions_len: Existing TLV bytes, excluding SECURITY
 * \param[in]       auth_tag_len: Zero or the selected authentication tag size
 * \param[out]      layout: Checked sizes and a zero epoch, cleared on failure
 * \return          XGL_OK or a length/capacity error
 */
xgl_error_t xgl_frame_measure_lengths(size_t payload_len, size_t extensions_len,
                                      size_t auth_tag_len,
                                      xgl_frame_layout_t* layout);

/**
 * \brief           Validate borrowed spans and measure the complete frame
 * \param[in]       frame: Logical header and borrowed payload/extensions
 * \param[in]       auth_tag_len: Zero, or a requested authentication trailer
 * \param[out]      layout: Checked lengths, cleared on failure
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_measure(const xgl_frame_t* frame, size_t auth_tag_len,
                              xgl_frame_layout_t* layout);

/**
 * \brief           Encode header, extensions and payload without authentication
 * \param[out]      buffer: Complete frame storage, including tag/CRC capacity
 * \param[in]       buffer_size: Available capacity in bytes
 * \param[in]       frame: Logical frame and source spans
 * \param[in]       auth_tag_len: Zero, or the reserved tag length
 * \param[out]      layout: Measured layout, written before checking capacity
 * \return          XGL_OK on success, error code otherwise
 * \note            Place an in-place payload at the measured payload offset.
 *                  Otherwise keep source spans separate from the destination.
 *                  A nonzero tag length reserves SECURITY and the tag. Fill
 *                  both before finalizing the CRC.
 */
xgl_error_t xgl_frame_encode_into(uint8_t* buffer, size_t buffer_size,
                                  const xgl_frame_t* frame, size_t auth_tag_len,
                                  xgl_frame_layout_t* layout);

/**
 * \brief           Finalize an encoded frame after its optional tag is ready
 * \param[in,out]   buffer: Encoded header, payload and optional tag
 * \param[in]       buffer_size: Available capacity in bytes
 * \param[in]       layout: Layout returned by the encoder
 * \param[out]      bytes_written: Complete frame length, zero on failure
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_frame_finalize_crc(uint8_t* buffer, size_t buffer_size,
                                   const xgl_frame_layout_t* layout,
                                   size_t* bytes_written);

/**
 * \brief           Calculate frame size
 * \param[in]       payload_len: Payload length in bytes
 * \return          Total frame size in bytes, or zero on invalid length
 */
static inline size_t xgl_frame_calculate_size(size_t payload_len) {
    xgl_frame_layout_t layout;
    return xgl_frame_measure_lengths(payload_len, 0U, 0U, &layout) == XGL_OK
               ? layout.frame_len
               : 0U;
}

/** \brief           Return the encoded byte size, or zero on invalid lengths.
 */
static inline size_t xgl_frame_serialized_size(size_t payload_len,
                                               size_t extensions_len,
                                               size_t auth_tag_len) {
    xgl_frame_layout_t layout;
    return xgl_frame_measure_lengths(payload_len, extensions_len, auth_tag_len,
                                     &layout) == XGL_OK
               ? layout.frame_len
               : 0U;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/** \brief           Compute the payload budget in bytes, zeroing it on error.
 */
static inline bool xgl_frame_payload_budget(size_t max_frame_size,
                                            size_t extensions_len,
                                            size_t auth_tag_len,
                                            size_t* payload_budget) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (payload_budget == NULL) {
        return false;
    }

    *payload_budget = 0U;
    xgl_frame_layout_t layout;
    if (xgl_frame_measure_lengths(0U, extensions_len, auth_tag_len, &layout) !=
            XGL_OK ||
        max_frame_size < layout.frame_len) {
        return false;
    }

    size_t available = max_frame_size - layout.frame_len;
    *payload_budget = available < UINT16_MAX ? available : UINT16_MAX;
    return true;
}

/*---------------------------------------------------------------------------*/
/* Traffic-Class Helper Functions                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Set reliability class in traffic-class bits
 * \param[in,out]   traffic_class: Traffic-class byte
 * \param[in]       reliable: Reliable transmission flag
 */
static inline void xgl_frame_set_reliability(uint8_t* traffic_class,
                                             bool reliable) {
    uint8_t reliable_bits =
        reliable ? XGL_RELIABILITY_ACK_ELICITING : XGL_RELIABILITY_NONE;
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
 * \brief           Set priority in traffic-class bits
 * \param[in,out]   traffic_class: Traffic-class byte
 * \param[in]       priority: Priority level (0-7)
 */
static inline void xgl_frame_set_priority(uint8_t* traffic_class,
                                          uint8_t priority) {
    *traffic_class =
        (uint8_t)(((uint32_t)*traffic_class &
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
