/**
 * \file            xgl_network.h
 * \brief           Network layer packet handling and routing
 * \author          Nexus Team
 */

#ifndef XGL_NETWORK_H
#define XGL_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_types.h"
#include "xgl_error.h"
#include "xgl_route.h"
#include "xgl_packet_pool.h"
#include "xgl_layer_interface.h"

/*---------------------------------------------------------------------------*/
/* Network Layer Configuration                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Protocol version
 */
#define XGL_PROTOCOL_VERSION        2

/**
 * \brief           Broadcast address
 */
#define XGL_BROADCAST_ID            0xFFFFU

/**
 * \brief           Default hop limit for routed packets
 */
#ifndef XGL_DEFAULT_TTL
#define XGL_DEFAULT_TTL             8
#endif

/*---------------------------------------------------------------------------*/
/* Forward Declarations                                                      */
/*---------------------------------------------------------------------------*/

/* Forward declare transport context */
struct xgl_transport_ctx_s;

/* Forward declare datalink context */
struct xgl_datalink_ctx_s;

/*---------------------------------------------------------------------------*/
/* Network Layer Context                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Network layer context structure
 */
typedef struct xgl_network_ctx_s {
    uint16_t local_id;              /**< Local node ID */
    xgl_route_table_t* route_table; /**< Route table */
    
    /* Layer interfaces for decoupled communication */
    xgl_layer_interface_t* upper_layer;  /**< Upper layer interface (transport) */
    xgl_layer_interface_t* lower_layer;  /**< Lower layer interface (datalink) */
    
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;       /**< User data for callbacks */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    bool auth_required;             /**< Require authenticated routed frames */
    uint32_t auth_key_id;           /**< Active authentication key id */
    xgl_auth_provider_t* auth_provider; /**< Authentication provider for forwarding */
} xgl_network_ctx_t;

/**
 * \brief           Network layer configuration structure
 */
typedef struct {
    uint16_t local_id;              /**< Local node ID */
    xgl_route_table_t* route_table; /**< Route table */
    xgl_layer_interface_t* upper_layer;  /**< Upper layer interface (can be NULL) */
    xgl_layer_interface_t* lower_layer;  /**< Lower layer interface (can be NULL) */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data;       /**< User data for callbacks (can be NULL) */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer (can be NULL) */
    bool auth_required;             /**< Require authenticated routed frames */
    uint32_t auth_key_id;           /**< Active authentication key id */
    xgl_auth_provider_t* auth_provider; /**< Authentication provider for forwarding */
} xgl_network_config_t;

/*---------------------------------------------------------------------------*/
/* Network Layer API                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize network layer context with configuration structure
 * \param[in,out]   ctx: Network layer context
 * \param[in]       config: Configuration structure
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_network_init(xgl_network_ctx_t* ctx,
                             const xgl_network_config_t* config);

/**
 * \brief           Send packet through network layer
 * \param[in]       ctx: Network layer context
 * \param[in]       packet: Packet to send
 * \param[in]       assign_packet_number: Packet-number assignment flag
 * \return          XGL_OK on success, error code otherwise
 * \note            This function performs routing and forwards packet to data link layer
 */
xgl_error_t xgl_network_send(xgl_network_ctx_t* ctx,
                             xgl_packet_t* packet,
                             bool assign_packet_number);

/**
 * \brief           Receive and process packet from data link layer
 * \param[in]       ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       frame_buf: Frame buffer
 * \param[in]       frame_len: Frame length
 * \return          XGL_OK on success, error code otherwise
 * \note            This function validates address and forwards to transport layer or application
 */
xgl_error_t xgl_network_receive(xgl_network_ctx_t* ctx,
                                xgl_handle_t handle,
                                const uint8_t* frame_buf,
                                size_t frame_len);

/**
 * \brief           Validate packet addressing
 * \param[in]       ctx: Network layer context
 * \param[in]       target_id: Target node ID
 * \param[in]       source_id: Source node ID
 * \return          true if addressing is valid, false otherwise
 */
bool xgl_network_validate_address(const xgl_network_ctx_t* ctx,
                                  uint16_t target_id,
                                  uint16_t source_id);

/**
 * \brief           Check if packet is addressed to local node
 * \param[in]       ctx: Network layer context
 * \param[in]       target_id: Target node ID
 * \return          true if packet is for local node, false otherwise
 */
static inline bool xgl_network_is_local(const xgl_network_ctx_t* ctx,
                                       uint16_t target_id) {
    return (target_id == ctx->local_id) || (target_id == XGL_BROADCAST_ID);
}

/**
 * \brief           Invoke error callback
 * \param[in]       ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message
 */
void xgl_network_report_error(xgl_network_ctx_t* ctx,
                              xgl_handle_t handle,
                              xgl_error_t error,
                              const char* message);

/**
 * \brief           Get network layer interface
 * \details         Returns the layer interface for this network instance
 * \param[in]       ctx: Network layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_network_get_interface(xgl_network_ctx_t* ctx,
                                     xgl_layer_interface_t* iface);

#ifdef __cplusplus
}
#endif

#endif /* XGL_NETWORK_H */
