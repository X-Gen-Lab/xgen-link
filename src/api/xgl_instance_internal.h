/**
 * \file            xgl_instance_internal.h
 * \brief           Internal instance structure definition
 * \author          X-Gen Lab
 */

#ifndef XGL_INSTANCE_INTERNAL_H
#define XGL_INSTANCE_INTERNAL_H

#include <datalink/xgl_datalink.h>
#include <datalink/xgl_parser.h>
#include <internal/xgl_protocol_io.h>
#include <internal/xgl_protocol_memory.h>
#include <network/xgl_network.h>
#include <network/xgl_route.h>
#include <transport/xgl_rtt.h>
#include <transport/xgl_transport.h>
#include <transport/xgl_window.h>
#include <xgl/xgl.h>
#include <xgl/xgl_diagnostics.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Internal Instance Structure                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Unified layer contexts manager
 * \details         Owns layer state and typed synchronous boundaries.
 */
typedef struct xgl_layer_contexts_s {
    /* Layer contexts - embedded directly */
    xgl_datalink_ctx_t datalink_ctx;   /**< Data link layer context */
    xgl_network_ctx_t network_ctx;     /**< Network layer context */
    xgl_transport_ctx_t transport_ctx; /**< Transport layer context */

    /* Layer interfaces for decoupled communication */
    xgl_frame_tx_endpoint_t datalink_tx;
    xgl_frame_rx_endpoint_t network_rx;
    xgl_packet_tx_endpoint_t network_tx;
    xgl_packet_rx_endpoint_t transport_rx;
} xgl_layer_contexts_t;

/**
 * \brief           Protocol instance internal structure
 */
typedef struct {
    xgl_phy_ops_t* phy;
    xgl_parser_t parser;
    uint32_t poll_interval_ms;
    uint32_t last_poll_ms;
    bool polled;
} xgl_instance_link_t;

struct xgl_instance {
    /*-----------------------------------------------------------------------*/
    /* Configuration                                                         */
    /*-----------------------------------------------------------------------*/
    const xgl_config_t* config; /**< Instance configuration */
    bool initialized;           /**< Initialization flag */
    uint32_t parser_timeout_ms; /**< Latest step policy; zero disables expiry */

    /*-----------------------------------------------------------------------*/
    /* Memory Management                                                     */
    /*-----------------------------------------------------------------------*/
    const xgl_allocator_t* allocator;
    const xgl_allocator_t*
        storage_allocator; /**< NULL for caller-owned workspace */
    const xgl_protocol_memory_t* memory;

    /*-----------------------------------------------------------------------*/
    /* Routing                                                               */
    /*-----------------------------------------------------------------------*/
    xgl_route_table_t route_table; /**< Route table */

    xgl_instance_link_t* links; /**< One RX parser per unique PHY */
    size_t link_count;

    /*-----------------------------------------------------------------------*/
    /* Protocol Stack Layers (Unified Management)                            */
    /*-----------------------------------------------------------------------*/
    xgl_layer_contexts_t layers; /**< Unified layer contexts and interfaces */
#if XGL_FEATURE_AUTH
    xgl_security_ctx_t security; /**< Endpoint state shared by all PHY links */
#endif

    /*-----------------------------------------------------------------------*/
    /* Statistics                                                            */
    /*-----------------------------------------------------------------------*/
#if XGL_FEATURE_STATISTICS
    xgl_statistics_t stats; /**< Protocol statistics */
#endif
};

#if XGL_FEATURE_STATISTICS
#define XGL_INSTANCE_STAT(handle, member) (&(handle)->stats.member)
#else
#define XGL_INSTANCE_STAT(handle, member) NULL
#endif

xgl_error_t xgl_instance_init_links(xgl_handle_t handle);
void xgl_instance_destroy_links(xgl_handle_t handle);
size_t xgl_instance_count_links(const xgl_config_t* config);

/** \brief           Validate instance policy and all workspace parameters. */
xgl_error_t xgl_instance_validate_config(const xgl_config_t* config);

#ifdef __cplusplus
}
#endif

#endif /* XGL_INSTANCE_INTERNAL_H */
