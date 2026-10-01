/**
 * \file            xgl_workspace_internal.h
 * \brief           Private workspace layout shared with target ABI diagnostics
 */
#ifndef XGL_WORKSPACE_INTERNAL_H
#define XGL_WORKSPACE_INTERNAL_H

#include <xgen/memory/pool.h>
#include <xgen/memory/size_class_allocator.h>

#include "xgl_instance_internal.h"

#define XGL_INIT_CLASSES (3U + 2U * XGL_FEATURE_ROUTE_INDEX)

/**
 * \brief           Resource identifiers present in the selected profile
 */
typedef enum {
    XGL_RESOURCE_PEER,
    XGL_RESOURCE_WINDOW,
    XGL_RESOURCE_TX_PACKET,
    XGL_RESOURCE_TX_PAYLOAD,
    XGL_RESOURCE_SCRATCH,
#if XGL_FEATURE_OUT_OF_ORDER
    XGL_RESOURCE_RX_PACKET,
    XGL_RESOURCE_RX_PAYLOAD,
    XGL_RESOURCE_RX_EXTENSIONS,
#endif
#if XGL_FEATURE_FRAGMENTATION
    XGL_RESOURCE_TX_EXTENSIONS,
    XGL_RESOURCE_TX_MESSAGE,
    XGL_RESOURCE_REASSEMBLY,
    XGL_RESOURCE_REASSEMBLY_PAYLOAD,
#endif
    XGL_RESOURCE_COUNT
} xgl_workspace_resource_t;

/**
 * \brief           Instance and independent reusable resource descriptors
 * \note            The instance is first so its handle is the workspace
 * address.
 */
typedef struct {
    struct xgl_instance instance;
    xgl_protocol_memory_t memory;
    xgm_size_class_allocator_t initialization;
    xgm_pool_t initial_pools[XGL_INIT_CLASSES];
    xgm_pool_t runtime_pools[XGL_RESOURCE_COUNT];
    xgm_allocator_t runtime_services[XGL_RESOURCE_COUNT];
} xgl_workspace_t;

#endif /* XGL_WORKSPACE_INTERNAL_H */
