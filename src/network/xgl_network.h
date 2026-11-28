/**
 * \file            xgl_network.h
 * \brief           Network layer packet handling and routing
 * \author          X-Gen Lab
 */

#ifndef XGL_NETWORK_H
#define XGL_NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "internal/xgl_packet.h"
#include "internal/xgl_protocol_io.h"
#include "network/xgl_route.h"
#include "security/xgl_security.h"
#include "xgl/xgl_alloc.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_diagnostics.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_inplace.h"
#include "xgl/xgl_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Network Layer Configuration                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Broadcast address
 */
#define XGL_BROADCAST_ID 0xFFFFU

/**
 * \brief           Default hop limit for routed packets
 */
#ifndef XGL_DEFAULT_TTL
#define XGL_DEFAULT_TTL 8
#endif

/*---------------------------------------------------------------------------*/
/* Network Layer Context                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Network layer context structure
 */
typedef struct xgl_network_ctx_s {
    uint16_t local_id;     /**< Local node ID */
    size_t max_frame_size; /**< Instance frame limit; zero uses the route MTU */
    xgl_route_table_t* route_table; /**< Route table */

    /* Layer interfaces for decoupled communication */
    xgl_packet_rx_endpoint_t*
        upper_layer; /**< Upper layer interface (transport) */
    xgl_frame_tx_endpoint_t*
        lower_layer; /**< Lower layer interface (datalink) */

    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;            /**< User data for callbacks */
#if XGL_FEATURE_STATISTICS
    xgl_layer_stats_t* stats; /**< Layer statistics pointer */
#endif
#if XGL_FEATURE_AUTH
    xgl_security_ctx_t* security; /**< Borrowed instance security state */
#endif
#if XGL_FEATURE_FORWARDING
    const xgl_allocator_t*
        allocator; /**< Allocator for forwarded frame copies */
#endif
} xgl_network_ctx_t;

/**
 * \brief           Network layer configuration structure
 */
typedef struct {
    uint16_t local_id;     /**< Local node ID */
    size_t max_frame_size; /**< Instance frame limit; zero uses the route MTU */
    xgl_route_table_t* route_table; /**< Route table */
    xgl_packet_rx_endpoint_t*
        upper_layer; /**< Upper layer interface (can be NULL) */
    xgl_frame_tx_endpoint_t*
        lower_layer; /**< Lower layer interface (can be NULL) */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data; /**< User data for callbacks (can be NULL) */
    xgl_layer_stats_t* stats; /**< Layer statistics pointer (can be NULL) */
#if XGL_FEATURE_AUTH
    xgl_security_ctx_t* security; /**< Local receive security state */
#endif
    const xgl_allocator_t*
        allocator; /**< Allocator for forwarded frame copies */
} xgl_network_config_t;

/*---------------------------------------------------------------------------*/
/* Network Layer API                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize network layer context with configuration
 * structure
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
 * \return          XGL_OK on success, error code otherwise
 * \note            This function performs routing and forwards packet to data
 * link layer
 */
xgl_error_t xgl_network_send(xgl_network_ctx_t* ctx, xgl_packet_t* packet);

/** \brief           Query one scoped frame without allocating or transmitting.
 */
xgl_error_t
xgl_network_zerocopy_layout(xgl_network_ctx_t* ctx,
                            const xgl_tx_zerocopy_request_t* request,
                            xgl_tx_frame_layout_t* layout);

/**
 * \brief           Route and synchronously submit scoped caller-owned storage
 * \param[in,out]   ctx: Network context
 * \param[in]       handle: Instance passed to synchronous callbacks
 * \param[in]       request: Borrowed payload, exact frame headroom and scope
 * \return          XGL_OK, or an argument, route, layout or submission error
 */
xgl_error_t
xgl_network_send_zerocopy_scoped(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                 const xgl_tx_zerocopy_request_t* request);

/**
 * \brief           Receive and process packet from data link layer
 * \param[in]       ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       frame_buf: Frame buffer
 * \param[in]       frame_len: Frame length
 * \return          XGL_OK on success, error code otherwise
 * \note            This separate raw entry checks the instance frame limit and
 *                  decodes bytes before local delivery or forwarding. Normal
 *                  datalink endpoints supply a validated view directly.
 */
xgl_error_t xgl_network_receive(xgl_network_ctx_t* ctx, xgl_handle_t handle,
                                const uint8_t* frame_buf, size_t frame_len);

/**
 * \brief           Process a validated frame view from the internal RX path
 * \param[in,out]   ctx: Network layer context
 * \param[in]       handle: Protocol instance handle
 * \param[in]       view: Frame view after successful wire and datalink
 *                  validation
 * \return          XGL_OK on success, error code otherwise
 * \note            The view and its borrowed bytes must remain valid throughout
 *                  this call. This entry point does not repeat validation.
 */
xgl_error_t xgl_network_receive_view(xgl_network_ctx_t* ctx,
                                     xgl_handle_t handle,
                                     const xgl_wire_frame_view_t* view);

/**
 * \brief           Validate packet addressing
 * \param[in]       ctx: Network layer context
 * \param[in]       target_id: Target node ID
 * \param[in]       source_id: Source node ID
 * \return          true if addressing is valid, false otherwise
 */
bool xgl_network_validate_address(const xgl_network_ctx_t* ctx,
                                  uint16_t target_id, uint16_t source_id);

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
 * \brief           Get network layer interface
 * \details         Returns the layer interface for this network instance
 * \param[in]       ctx: Network layer context
 * \param[out]      packets: Synchronous logical packet transmitter
 * \param[out]      frames: Receiver requiring a validated borrowed frame view
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_network_get_endpoints(xgl_network_ctx_t* ctx,
                                      xgl_packet_tx_endpoint_t* packets,
                                      xgl_frame_rx_endpoint_t* frames);

#ifdef __cplusplus
}
#endif

#endif /* XGL_NETWORK_H */
