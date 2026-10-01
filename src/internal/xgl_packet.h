/**
 * \file            xgl_packet.h
 * \brief           Borrowed protocol packet metadata
 * \author          X-Gen Lab
 */

#ifndef XGL_PACKET_H
#define XGL_PACKET_H

#include "xgl/xgl_types.h"

/** \brief           Payload span borrowed for a synchronous layer call. */
typedef struct {
    size_t data_len;
    const uint8_t* data;
} xgl_packet_data_t;

typedef struct xgl_packet {
    /*-----------------------------------------------------------------------*/
    /* Addressing                                                            */
    /*-----------------------------------------------------------------------*/
    uint16_t source_id;     /**< Source node ID */
    uint16_t target_id;     /**< Target node ID */
    uint32_t connection_id; /**< Production connection context ID */
    uint32_t packet_number; /**< Monotonic production packet number */
    uint32_t session_epoch; /**< Production session epoch */

    /*-----------------------------------------------------------------------*/
    /* Attributes                                                            */
    /*-----------------------------------------------------------------------*/
    uint8_t version;       /**< Protocol version */
    uint8_t packet_type;   /**< Production packet type */
    uint8_t flags;         /**< Production wire flags */
    uint8_t data_type;     /**< Data type */
    uint8_t reliable;      /**< Reliable transmission flag */
    uint8_t fragment;      /**< Fragment flag */
    uint8_t encrypt;       /**< Encryption type */
    uint8_t priority;      /**< Priority level (0-7) */
    uint8_t ttl;           /**< Hop limit */
    uint8_t traffic_class; /**< Priority and traffic class */
    uint8_t compress;      /**< Compression type */

    /*-----------------------------------------------------------------------*/
    /* Data                                                                  */
    /*-----------------------------------------------------------------------*/
    xgl_packet_data_t* data;   /**< Pointer to packet data */
    const uint8_t* extensions; /**< Production TLV extension bytes */
    size_t extensions_len;     /**< Length of production TLV extensions */

    /*-----------------------------------------------------------------------*/
    /* Routing                                                               */
    /*-----------------------------------------------------------------------*/
    xgl_phy_ops_t* phy; /**< Physical layer operations */

} xgl_packet_t;

#endif /* XGL_PACKET_H */
