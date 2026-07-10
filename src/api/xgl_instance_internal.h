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

#include <xgl/xgl.h>
#include <xgl/internal/xgl_tiered_pool.h>
#include <xgl/internal/xgl_packet_pool.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_window.h>
#include <xgl/internal/xgl_rtt.h>
#include <xgl/internal/xgl_parser.h>
#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_transport.h>
#include <xgl/internal/xgl_layer_interface.h>
#include <xgl/internal/xgl_codec.h>

/*---------------------------------------------------------------------------*/
/* Internal Instance Structure                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Unified layer contexts manager
 * \details         Manages all protocol layer contexts and interfaces in one place.
 *                  This structure provides a clean separation between layer contexts
 *                  and their interfaces, following the design pattern from
 *                  LAYER_DECOUPLING_DESIGN.md
 */
typedef struct xgl_layer_contexts_s {
    /* Layer contexts - embedded directly */
    xgl_datalink_ctx_t datalink_ctx;   /**< Data link layer context */
    xgl_network_ctx_t network_ctx;     /**< Network layer context */
    xgl_transport_ctx_t transport_ctx; /**< Transport layer context */

    /* Layer interfaces for decoupled communication */
    xgl_layer_interface_t datalink_iface;  /**< Datalink interface */
    xgl_layer_interface_t network_iface;   /**< Network interface */
    xgl_layer_interface_t transport_iface; /**< Transport interface */
} xgl_layer_contexts_t;

/**
 * \brief           Protocol instance internal structure
 */
struct xgl_instance {
    /*-----------------------------------------------------------------------*/
    /* Configuration                                                         */
    /*-----------------------------------------------------------------------*/
    xgl_config_t config;            /**< Instance configuration */
    bool initialized;               /**< Initialization flag */

    /*-----------------------------------------------------------------------*/
    /* Memory Management                                                     */
    /*-----------------------------------------------------------------------*/
    xgl_allocator_t* allocator;     /**< Memory allocator */
    xgl_tiered_pool_t tx_pool;      /**< Tiered TX memory pool */
    xgl_packet_pool_t packet_pool;  /**< Packet object pool */

    /*-----------------------------------------------------------------------*/
    /* Routing                                                               */
    /*-----------------------------------------------------------------------*/
    xgl_route_table_t route_table;  /**< Route table */

    uint32_t* route_last_read_ms;   /**< Last RX polling timestamp per configured route */
    size_t route_last_read_count;   /**< Number of route polling timestamps */

    /*-----------------------------------------------------------------------*/
    /* Protocol Stack Layers (Unified Management)                            */
    /*-----------------------------------------------------------------------*/
    xgl_layer_contexts_t layers;    /**< Unified layer contexts and interfaces */

    /*-----------------------------------------------------------------------*/
    /* Statistics                                                            */
    /*-----------------------------------------------------------------------*/
    xgl_statistics_t stats;         /**< Protocol statistics */

    /*-----------------------------------------------------------------------*/
    /* Codec Registry                                                        */
    /*-----------------------------------------------------------------------*/
    xgl_codec_registry_t codec_registry; /**< Instance-scoped codec registry */
    xgl_codec_t codec_storage[4];   /**< Storage for up to 4 registered codecs */

    /*-----------------------------------------------------------------------*/
    /* Thread Safety                                                         */
    /*-----------------------------------------------------------------------*/
#ifdef XGL_THREAD_SAFE
    xgl_mutex_t mutex;              /**< Instance mutex */
#endif
};

/*---------------------------------------------------------------------------*/
/* Thread-Safety Guard Macros (internal only)                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Lock instance mutex if thread-safe mode is active
 * \return          XGL_OK on success, error code on lock failure
 */
static inline xgl_error_t xgl_instance_lock(xgl_handle_t h) {
#ifdef XGL_THREAD_SAFE
    if (h->config.features.thread_safe) {
        return xgl_mutex_lock(&h->mutex);
    }
#else
    (void)h;
#endif
    return XGL_OK;
}

/**
 * \brief           Unlock instance mutex if thread-safe mode is active
 */
static inline void xgl_instance_unlock(xgl_handle_t h) {
#ifdef XGL_THREAD_SAFE
    if (h->config.features.thread_safe) {
        xgl_mutex_unlock(&h->mutex);
    }
#else
    (void)h;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_INSTANCE_INTERNAL_H */

