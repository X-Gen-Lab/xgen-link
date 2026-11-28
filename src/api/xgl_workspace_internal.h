/**
 * \file            xgl_workspace_internal.h
 * \brief           Private workspace layout shared with target ABI diagnostics
 * \author          X-Gen Lab
 */
#ifndef XGL_WORKSPACE_INTERNAL_H
#define XGL_WORKSPACE_INTERNAL_H

#include <xgl/xgl_alloc.h>

#include "internal/xgl_size_class_allocator.h"

#include "xgl_instance_internal.h"

#define XGL_INIT_CLASSES (3U + 2U * XGL_FEATURE_ROUTE_INDEX)

/**
 * \brief           Resource identifiers present in the selected profile
 */
typedef enum {
    XGL_RESOURCE_PEER,
    XGL_RESOURCE_TX_PACKET,
    XGL_RESOURCE_TX_PAYLOAD,
    XGL_RESOURCE_SCRATCH,
#if XGL_FEATURE_OUT_OF_ORDER
    XGL_RESOURCE_RX_PACKET,
    XGL_RESOURCE_RX_PAYLOAD,
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
    xgl_size_class_allocator_t initialization;
    xgl_pool_t initial_pools[XGL_INIT_CLASSES];
    xgl_pool_t runtime_pools[XGL_RESOURCE_COUNT];
    xgl_allocator_t runtime_services[XGL_RESOURCE_COUNT];
} xgl_workspace_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Bind workspace resources before the protocol layers start
 * \note            NULL storage selects an owned workspace using allocator.
 *                  Returning before layer startup bounds temporary stack use.
 */
xgl_error_t xgl_workspace_prepare_instance(const xgl_config_t* config,
                                           void* storage, size_t storage_size,
                                           const xgl_allocator_t* allocator,
                                           xgl_handle_t* prepared);

#ifdef __cplusplus
}
#endif

#endif /* XGL_WORKSPACE_INTERNAL_H */
