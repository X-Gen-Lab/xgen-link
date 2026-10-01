/**
 * \file            xgl_workspace.c
 * \brief           Exact protocol resource partitions over bounded memory pools
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_protocol_memory.h>
#include <xgl/internal/xgl_wire.h>

#include <string.h>
#include <xgen/containers/bitset.h>
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

/**
 * \brief           Validated partition sizes shared by both creation paths
 */
typedef struct {
    xgm_size_class_spec_t initial[XGL_INIT_CLASSES];
    xgm_size_class_spec_t runtime[XGL_RESOURCE_COUNT];
    size_t initial_count;
    size_t prefix_size;
    size_t initialization_size;
    xgl_memory_requirements_t requirements;
} xgl_workspace_plan_t;

/**
 * \brief           Add protocol partition sizes without unsigned overflow
 */
static bool add_size(size_t a, size_t b, size_t* result) {
    if (a > SIZE_MAX - b) {
        return false;
    }
    *result = a + b;
    return true;
}

/**
 * \brief           Multiply a protocol partition size without overflow
 */
static bool multiply_size(size_t count, size_t size, size_t* result) {
    if (size != 0U && count > SIZE_MAX / size) {
        return false;
    }
    *result = count * size;
    return true;
}

/**
 * \brief           Round a resource block up to the public allocator alignment
 */
static bool align_size(size_t size, size_t* result) {
    const size_t alignment = _Alignof(xgm_max_align_t);
    size_t rounded;
    if (size < sizeof(void*)) {
        size = sizeof(void*);
    }
    if (!add_size(size, alignment - 1U, &rounded)) {
        return false;
    }
    *result = rounded / alignment * alignment;
    return true;
}

/**
 * \brief           Count distinct PHY descriptors in the configured routes
 */
static size_t unique_links(const xgl_config_t* config) {
    size_t count = 0U;
    for (size_t i = 0U; i < config->route_table_len; ++i) {
        size_t j = 0U;
        for (; j < i; ++j) {
            if (config->route_table[j].phy == config->route_table[i].phy) {
                break;
            }
        }
        if (j == i) {
            ++count;
        }
    }
    return count;
}

/**
 * \brief           Reserve an initialization object or array
 */
static bool add_initial(xgl_workspace_plan_t* plan, size_t size, size_t count) {
    if (count == 0U) {
        return true;
    }
    if (plan->initial_count >= XGL_INIT_CLASSES || size == 0U) {
        return false;
    }
    plan->initial[plan->initial_count++] = (xgm_size_class_spec_t){size, count};
    return true;
}

/**
 * \brief           Reserve one independently bounded runtime resource class
 */
static bool set_resource(xgl_workspace_plan_t* plan,
                         xgl_workspace_resource_t resource, size_t size,
                         size_t count) {
    size_t bytes;
    if (count == 0U) {
        return true;
    }
    if (size == 0U || !align_size(size, &size) ||
        !multiply_size(count, size, &bytes) ||
        !add_size(plan->requirements.size, bytes, &plan->requirements.size) ||
        !add_size(plan->requirements.runtime_blocks, count,
                  &plan->requirements.runtime_blocks)) {
        return false;
    }
    plan->runtime[resource] = (xgm_size_class_spec_t){size, count};
    if (plan->requirements.runtime_block_size < size) {
        plan->requirements.runtime_block_size = size;
    }
    return true;
}

/**
 * \brief           Plan fixed route, link and receive-parser storage
 */
static bool plan_initialization(const xgl_config_t* config,
                                xgl_workspace_plan_t* plan) {
    const size_t routes = config->route_table_len;
    const size_t links = unique_links(config);
    size_t bytes;
    if (!multiply_size(routes, sizeof(xgl_route_item_t), &bytes) ||
        !add_initial(plan, bytes, routes != 0U ? 1U : 0U)) {
        return false;
    }
#if XGL_FEATURE_ROUTE_INDEX
    if (routes != 0U &&
        (!add_initial(plan,
                      XGL_ROUTE_TABLE_DEFAULT_SIZE * sizeof(xgct_hash_node_t*),
                      1U) ||
         !multiply_size(routes, sizeof(xgct_hash_node_t), &bytes) ||
         !add_initial(plan, bytes, 1U))) {
        return false;
    }
#endif
    if (!add_initial(plan, config->memory.rx_buffer_size,
                     links != 0U ? links : 1U) ||
        !multiply_size(routes, sizeof(xgl_instance_link_t), &bytes) ||
        !add_initial(plan, bytes, routes != 0U ? 1U : 0U)) {
        return false;
    }
    return xgm_size_class_measure(plan->initial, plan->initial_count,
                                  &plan->initialization_size,
                                  &plan->requirements.alignment) == XGS_OK &&
           align_size(sizeof(xgl_workspace_t), &plan->prefix_size) &&
           add_size(plan->prefix_size, plan->initialization_size,
                    &plan->requirements.size);
}

/**
 * \brief           Plan optional out-of-order and message retention classes
 */
static bool plan_optional_resources(const xgl_config_t* config,
                                    xgl_workspace_plan_t* plan) {
#if XGL_FEATURE_OUT_OF_ORDER
    const size_t rx_count = config->features.max_rx_buffered_packets;
    const size_t payload_size = config->protocol.max_frame_size -
                                XGL_WIRE_BASE_HEADER_SIZE - XGL_CRC16_SIZE;
    const size_t extension_limit = UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE;
    const size_t extension_size =
        payload_size < extension_limit ? payload_size : extension_limit;
    if (!set_resource(plan, XGL_RESOURCE_RX_PACKET,
                      sizeof(xgl_transport_rx_buffered_packet_t), rx_count) ||
        !set_resource(plan, XGL_RESOURCE_RX_PAYLOAD, payload_size, rx_count) ||
        !set_resource(plan, XGL_RESOURCE_RX_EXTENSIONS, extension_size,
                      rx_count)) {
        return false;
    }
#endif
#if XGL_FEATURE_FRAGMENTATION
    if (config->features.enable_fragmentation) {
        const size_t peers = config->features.max_peers;
        const size_t slots = config->features.max_reassembly_slots;
        const size_t message_size = config->features.max_message_size;
        size_t retained;
        if (message_size == 0U || slots == 0U ||
            config->features.max_reassembly_bytes == 0U ||
            config->features.max_tx_message_bytes == 0U ||
            !add_size(slots, peers, &retained) ||
            !set_resource(plan, XGL_RESOURCE_TX_EXTENSIONS,
                          XGL_FRAGMENT_EXT_SIZE,
                          config->features.max_tx_packets) ||
            !set_resource(plan, XGL_RESOURCE_TX_MESSAGE, message_size, peers) ||
            !set_resource(plan, XGL_RESOURCE_REASSEMBLY,
                          sizeof(xgl_reassembly_buffer_t), slots) ||
            !set_resource(plan, XGL_RESOURCE_REASSEMBLY_PAYLOAD, message_size,
                          retained)) {
            return false;
        }
    }
#endif
    (void)config;
    (void)plan;
    return true;
}

/**
 * \brief           Calculate the exact layout used by every instance
 */
static xgl_error_t make_plan(const xgl_config_t* config,
                             xgl_workspace_plan_t* plan) {
    xgl_error_t error = xgl_config_validate(config);
    if (error != XGL_OK) {
        return error;
    }
    if (config->features.max_peers == 0U ||
        config->features.max_tx_packets == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    memset(plan, 0, sizeof(*plan));
    if (!plan_initialization(config, plan) ||
        !set_resource(plan, XGL_RESOURCE_PEER,
                      sizeof(xgl_transport_peer_state_t),
                      config->features.max_peers) ||
        !set_resource(plan, XGL_RESOURCE_WINDOW,
                      xgct_bitset_storage_size(config->protocol.window_size),
                      config->features.max_peers) ||
        !set_resource(plan, XGL_RESOURCE_TX_PACKET,
                      sizeof(xgl_reliable_packet_t),
                      config->features.max_tx_packets) ||
        !set_resource(plan, XGL_RESOURCE_TX_PAYLOAD,
                      config->protocol.max_frame_size -
                          XGL_WIRE_BASE_HEADER_SIZE - XGL_CRC16_SIZE,
                      config->features.max_tx_packets) ||
        /* Synchronous, non-reentrant PHY calls retain only one frame scratch.
         */
        !set_resource(plan, XGL_RESOURCE_SCRATCH,
                      config->protocol.max_frame_size, 1U) ||
        !plan_optional_resources(config, plan)) {
        return XGL_ERR_INVALID_PARAM;
    }
    return XGL_OK;
}

/**
 * \brief           Bind resource services without allocation or fallback
 */
static void bind_services(xgl_workspace_t* workspace) {
    xgl_protocol_memory_t* memory = &workspace->memory;
    xgm_allocator_t* services = workspace->runtime_services;
    memory->peer = &services[XGL_RESOURCE_PEER];
    memory->window = &services[XGL_RESOURCE_WINDOW];
    memory->tx_packet = &services[XGL_RESOURCE_TX_PACKET];
    memory->tx_payload = &services[XGL_RESOURCE_TX_PAYLOAD];
    memory->scratch = &services[XGL_RESOURCE_SCRATCH];
#if XGL_FEATURE_OUT_OF_ORDER
    memory->rx_packet = &services[XGL_RESOURCE_RX_PACKET];
    memory->rx_payload = &services[XGL_RESOURCE_RX_PAYLOAD];
    memory->rx_extensions = &services[XGL_RESOURCE_RX_EXTENSIONS];
#endif
#if XGL_FEATURE_FRAGMENTATION
    memory->tx_extensions = &services[XGL_RESOURCE_TX_EXTENSIONS];
    memory->tx_message = &services[XGL_RESOURCE_TX_MESSAGE];
    memory->reassembly = &services[XGL_RESOURCE_REASSEMBLY];
    memory->reassembly_payload = &services[XGL_RESOURCE_REASSEMBLY_PAYLOAD];
#endif
}

/**
 * \brief           Validate the caller ABI before calculating workspace sizes
 */
xgl_error_t
xgl_memory_requirements_checked(const xgl_config_t* config, size_t config_size,
                                uint32_t abi_version, uint32_t build_config_id,
                                xgl_memory_requirements_t* requirements) {
    if (config_size != sizeof(xgl_config_t) ||
        abi_version != XGL_CONFIG_ABI_VERSION ||
        build_config_id != XGL_BUILD_CONFIG_ID) {
        return XGL_ERR_INVALID_VERSION;
    }
    if (requirements == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_workspace_plan_t plan;
    xgl_error_t error = make_plan(config, &plan);
    if (error == XGL_OK) {
        *requirements = plan.requirements;
    }
    return error;
}

/**
 * \brief           Prepare bounded pools and an uninitialized protocol instance
 * \param[in]       config: Immutable configuration borrowed until destruction
 * \param[in,out]   storage: Aligned storage covering the measured layout
 * \param[in]       storage_size: Available storage bytes
 * \param[in]       storage_allocator: Owning backend, or NULL for caller
 * storage
 * \param[out]      handle: Prepared instance, or NULL on failure
 * \return          XGL_OK or a configuration, alignment or capacity error
 */
xgl_error_t xgl_workspace_prepare(const xgl_config_t* config, void* storage,
                                  size_t storage_size,
                                  const xgm_allocator_t* storage_allocator,
                                  xgl_handle_t* handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *handle = NULL;
    if (storage == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_workspace_plan_t plan;
    xgl_error_t error = make_plan(config, &plan);
    if (error != XGL_OK) {
        return error;
    }
    if (storage_size < plan.requirements.size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if ((uintptr_t)storage % plan.requirements.alignment != 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    xgl_workspace_t* workspace = storage;
    memset(workspace, 0, sizeof(*workspace));
    uint8_t* cursor = (uint8_t*)storage + plan.prefix_size;
    if (xgm_size_class_init(&workspace->initialization,
                            workspace->initial_pools, XGL_INIT_CLASSES,
                            plan.initial, plan.initial_count, cursor,
                            plan.initialization_size) != XGS_OK) {
        return XGL_ERR_INVALID_PARAM;
    }
    cursor += plan.initialization_size;
    for (size_t i = 0U; i < XGL_RESOURCE_COUNT; ++i) {
        const xgm_size_class_spec_t* spec = &plan.runtime[i];
        if (spec->block_count != 0U) {
            const size_t bytes = spec->block_size * spec->block_count;
            if (xgm_pool_init(&workspace->runtime_pools[i], cursor, bytes,
                              spec->block_size, plan.requirements.alignment,
                              spec->block_count) != XGS_OK) {
                return XGL_ERR_INVALID_PARAM;
            }
            cursor += bytes;
        }
        workspace->runtime_services[i] =
            xgm_pool_allocator(&workspace->runtime_pools[i]);
    }
    bind_services(workspace);
    workspace->instance.config = config;
    workspace->instance.allocator = &workspace->initialization.service;
    workspace->instance.storage_allocator = storage_allocator;
    workspace->instance.memory = &workspace->memory;
    workspace->instance.caller_owned = storage_allocator == NULL;
    *handle = &workspace->instance;
    return XGL_OK;
}

/**
 * \brief           Initialize the shared layout inside caller-owned storage
 */
xgl_error_t xgl_init_static_checked(const xgl_config_t* config,
                                    size_t config_size, uint32_t abi_version,
                                    uint32_t build_config_id, void* storage,
                                    size_t storage_size, xgl_handle_t* handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *handle = NULL;
    if (config_size != sizeof(xgl_config_t) ||
        abi_version != XGL_CONFIG_ABI_VERSION ||
        build_config_id != XGL_BUILD_CONFIG_ID) {
        return XGL_ERR_INVALID_VERSION;
    }
    xgl_handle_t prepared;
    xgl_error_t error =
        xgl_workspace_prepare(config, storage, storage_size, NULL, &prepared);
    if (error != XGL_OK) {
        return error;
    }
    error = xgl_init(prepared);
    if (error == XGL_OK) {
        *handle = prepared;
    }
    return error;
}
