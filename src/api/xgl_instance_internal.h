/**
 * \file            xgl_instance_internal.h
 * \brief           Internal instance structure definition
 * \author          X-Gen Lab
 */

#ifndef XGL_INSTANCE_INTERNAL_H
#define XGL_INSTANCE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_parser.h>
#include <xgl/internal/xgl_protocol_io.h>
#include <xgl/internal/xgl_protocol_memory.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_rtt.h>
#include <xgl/internal/xgl_transport.h>
#include <xgl/internal/xgl_window.h>
#include <xgl/xgl.h>

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
    xgl_frame_interface_t datalink_iface;
    xgl_frame_interface_t network_frame_iface;
    xgl_packet_interface_t network_packet_iface;
    xgl_packet_interface_t transport_iface;
} xgl_layer_contexts_t;

/**
 * \brief           Protocol instance internal structure
 */
typedef struct {
    xgl_phy_ops_t* phy;
    xgl_parser_t parser;
    uint8_t* rx_cache;
    uint32_t poll_interval_ms;
    uint32_t read_freq_hz;
    uint32_t last_poll_ms;
    bool polled;
} xgl_instance_link_t;

struct xgl_instance {
    /*-----------------------------------------------------------------------*/
    /* Configuration                                                         */
    /*-----------------------------------------------------------------------*/
    const xgl_config_t* config; /**< Instance configuration */
    bool initialized;           /**< Initialization flag */
    bool caller_owned;          /**< Instance resides inside caller workspace */

    /*-----------------------------------------------------------------------*/
    /* Memory Management                                                     */
    /*-----------------------------------------------------------------------*/
    const xgm_allocator_t* allocator;
    const xgm_allocator_t* storage_allocator;
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

    /*-----------------------------------------------------------------------*/
    /* Statistics                                                            */
    /*-----------------------------------------------------------------------*/
    xgl_statistics_t stats; /**< Protocol statistics */
};

xgl_error_t xgl_instance_init_links(xgl_handle_t handle);
void xgl_instance_destroy_links(xgl_handle_t handle);

/**
 * \brief           Prepare caller storage without initializing protocol layers
 */
xgl_error_t xgl_workspace_prepare(const xgl_config_t* config, void* storage,
                                  size_t storage_size,
                                  const xgm_allocator_t* storage_allocator,
                                  xgl_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* XGL_INSTANCE_INTERNAL_H */
