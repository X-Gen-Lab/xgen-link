/**
 * \file            xgl_workspace.c
 * \brief           Exact protocol resource partitions over bounded memory pools
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <internal/xgl_allocator.h>
#include <internal/xgl_protocol_memory.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl_alloc.h>

#include <string.h>

#include "xgl_workspace_internal.h"

_Static_assert(offsetof(xgl_workspace_t, instance) == 0U,
               "The instance handle must equal the owned workspace address");

/**
 * \brief           Validated partition sizes shared by both creation paths
 */
typedef struct {
    xgl_size_class_spec_t initial[XGL_INIT_CLASSES];
    xgl_size_class_spec_t runtime[XGL_RESOURCE_COUNT];
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
 * \brief           Reserve an initialization object or array
 */
static bool add_initial(xgl_workspace_plan_t* plan, size_t size, size_t count) {
    if (count == 0U) {
        return true;
    }
    if (plan->initial_count >= XGL_INIT_CLASSES || size == 0U) {
        return false;
    }
    plan->initial[plan->initial_count++] = (xgl_size_class_spec_t){size, count};
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
    if (!xgl_pool_measure(size, count, &size, &bytes) ||
        !add_size(plan->requirements.size, bytes, &plan->requirements.size) ||
        !add_size(plan->requirements.runtime_blocks, count,
                  &plan->requirements.runtime_blocks)) {
        return false;
    }
    plan->runtime[resource] = (xgl_size_class_spec_t){size, count};
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
    const size_t links = xgl_instance_count_links(config);
    size_t bytes;
    if (!multiply_size(routes, sizeof(xgl_route_item_t), &bytes) ||
        !add_initial(plan, bytes, routes != 0U ? 1U : 0U)) {
        return false;
    }
#if XGL_FEATURE_ROUTE_INDEX
    if (routes != 0U &&
        (!add_initial(plan,
                      XGL_ROUTE_TABLE_DEFAULT_SIZE *
                          sizeof(xgl_route_index_node_t*),
                      1U) ||
         !multiply_size(routes, sizeof(xgl_route_index_node_t), &bytes) ||
         !add_initial(plan, bytes, 1U))) {
        return false;
    }
#endif
    if (!add_initial(plan, config->memory.rx_buffer_size, links) ||
        !multiply_size(links, sizeof(xgl_instance_link_t), &bytes) ||
        !add_initial(plan, bytes, links != 0U ? 1U : 0U)) {
        return false;
    }
    plan->requirements.alignment = _Alignof(xgl_max_align_t);
    size_t prefix_stride;
    return xgl_size_class_measure(plan->initial, plan->initial_count,
                                  &plan->initialization_size) &&
           xgl_pool_measure(sizeof(xgl_workspace_t), 1U, &prefix_stride,
                            &plan->prefix_size) &&
           add_size(plan->prefix_size, plan->initialization_size,
                    &plan->requirements.size);
}

/**
 * \brief           Plan optional out-of-order storage
 */
static bool plan_rx_resources(const xgl_config_t* config,
                              xgl_workspace_plan_t* plan) {
#if XGL_FEATURE_OUT_OF_ORDER
    const size_t rx_count = config->features.max_rx_buffered_packets;
    const size_t payload_size = (size_t)config->protocol.max_frame_size -
                                XGL_WIRE_BASE_HEADER_SIZE - XGL_CRC16_SIZE;
    return set_resource(plan, XGL_RESOURCE_RX_PACKET,
                        sizeof(xgl_transport_rx_buffered_packet_t), rx_count) &&
           set_resource(plan, XGL_RESOURCE_RX_PAYLOAD, payload_size, rx_count);
#else
    (void)config;
    (void)plan;
    return true;
#endif
}

/**
 * \brief           Calculate the exact layout used by every instance
 */
static xgl_error_t make_plan(const xgl_config_t* config,
                             xgl_workspace_plan_t* plan) {
    xgl_error_t error = xgl_instance_validate_config(config);
    if (error != XGL_OK) {
        return error;
    }
    memset(plan, 0, sizeof(*plan));
    if (!plan_initialization(config, plan) ||
        !set_resource(plan, XGL_RESOURCE_PEER,
                      sizeof(xgl_transport_peer_state_t),
                      config->features.max_peers) ||
        !set_resource(plan, XGL_RESOURCE_TX_PACKET,
                      sizeof(xgl_reliable_packet_t),
                      config->features.max_tx_packets) ||
        !set_resource(plan, XGL_RESOURCE_TX_PAYLOAD,
                      (size_t)config->protocol.max_frame_size -
                          XGL_WIRE_BASE_HEADER_SIZE - XGL_CRC16_SIZE,
                      config->features.max_tx_packets) ||
        /* Synchronous, non-reentrant PHY calls retain only one frame scratch.
         */
        !set_resource(plan, XGL_RESOURCE_SCRATCH,
                      config->protocol.max_frame_size, 1U) ||
        !plan_rx_resources(config, plan)) {
        return XGL_ERR_INVALID_PARAM;
    }
    return XGL_OK;
}

/**
 * \brief           Bind resource services without allocation or fallback
 */
static void bind_services(xgl_workspace_t* workspace) {
    xgl_protocol_memory_t* memory = &workspace->memory;
    xgl_allocator_t* services = workspace->runtime_services;
    memory->peer = &services[XGL_RESOURCE_PEER];
    memory->tx_packet = &services[XGL_RESOURCE_TX_PACKET];
    memory->tx_payload = &services[XGL_RESOURCE_TX_PAYLOAD];
    memory->scratch = &services[XGL_RESOURCE_SCRATCH];
#if XGL_FEATURE_OUT_OF_ORDER
    memory->rx_packet = &services[XGL_RESOURCE_RX_PACKET];
    memory->rx_payload = &services[XGL_RESOURCE_RX_PAYLOAD];
#endif
}

/**
 * \brief           Calculate the exact static workspace requirements
 */
xgl_error_t xgl_memory_requirements(const xgl_config_t* config,
                                    xgl_memory_requirements_t* requirements) {
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

/** \brief           Read a validated resource partition's reserved size. */
static size_t resource_bytes(const xgl_workspace_plan_t* plan,
                             xgl_workspace_resource_t resource) {
    const xgl_size_class_spec_t* spec = &plan->runtime[resource];
    return spec->block_size * spec->block_count;
}

/** \brief           Describe an already validated storage plan. */
static void report_plan(const xgl_workspace_plan_t* plan,
                        xgl_memory_report_t* report) {
    xgl_memory_report_t result = {
        .requirements = plan->requirements,
        .management_bytes = plan->prefix_size,
        .initialization_bytes = plan->initialization_size,
        .peer_bytes = resource_bytes(plan, XGL_RESOURCE_PEER),
        .tx_record_bytes = resource_bytes(plan, XGL_RESOURCE_TX_PACKET),
        .tx_payload_bytes = resource_bytes(plan, XGL_RESOURCE_TX_PAYLOAD),
        .scratch_bytes = resource_bytes(plan, XGL_RESOURCE_SCRATCH),
    };
#if XGL_FEATURE_OUT_OF_ORDER
    result.rx_buffered_bytes = resource_bytes(plan, XGL_RESOURCE_RX_PACKET) +
                               resource_bytes(plan, XGL_RESOURCE_RX_PAYLOAD);
#endif
    *report = result;
}

/** \brief           Report the same validated layout used by initialization. */
xgl_error_t xgl_memory_report(const xgl_config_t* config,
                              xgl_memory_report_t* report) {
    if (report == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_workspace_plan_t plan;
    xgl_error_t error = make_plan(config, &plan);
    if (error != XGL_OK) {
        return error;
    }
    report_plan(&plan, report);
    return XGL_OK;
}

/** \brief           Populate caller storage from a validated plan. */
static xgl_error_t prepare_storage(const xgl_workspace_plan_t* plan,
                                   void* storage, size_t storage_size) {
    if (storage_size < plan->requirements.size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if ((uintptr_t)storage % _Alignof(xgl_max_align_t) != 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    xgl_workspace_t* workspace = storage;
    memset(workspace, 0, sizeof(*workspace));
    uint8_t* cursor = (uint8_t*)storage + plan->prefix_size;
    if (!xgl_size_class_init(
            &workspace->initialization, workspace->initial_pools, plan->initial,
            plan->initial_count, cursor, plan->initialization_size)) {
        return XGL_ERR_INVALID_PARAM;
    }
    cursor += plan->initialization_size;
    for (size_t i = 0U; i < XGL_RESOURCE_COUNT; ++i) {
        const xgl_size_class_spec_t* spec = &plan->runtime[i];
        if (spec->block_count != 0U) {
            const size_t bytes = spec->block_size * spec->block_count;
            if (!xgl_pool_init(&workspace->runtime_pools[i], cursor, bytes,
                               spec->block_size, spec->block_count)) {
                return XGL_ERR_INVALID_PARAM;
            }
            cursor += bytes;
        }
        workspace->runtime_services[i] =
            xgl_pool_allocator(&workspace->runtime_pools[i]);
    }
    bind_services(workspace);
    workspace->instance.allocator = &workspace->initialization.service;
    workspace->instance.memory = &workspace->memory;
#if XGL_FEATURE_STATISTICS
    workspace->instance.stats.memory_used = plan->requirements.size;
    workspace->instance.stats.memory_peak = plan->requirements.size;
#endif
    return XGL_OK;
}

xgl_error_t xgl_workspace_prepare_instance(const xgl_config_t* config,
                                           void* storage, size_t storage_size,
                                           const xgl_allocator_t* allocator,
                                           xgl_handle_t* prepared) {
    xgl_workspace_plan_t plan;
    xgl_error_t error = make_plan(config, &plan);
    if (error != XGL_OK) {
        return error;
    }
    if (storage == NULL) {
#if XGL_ALLOW_FALLBACK_MALLOC
        if (allocator == NULL) {
            allocator = xgl_allocator_libc();
        }
#endif
        if (!xgl_allocator_is_valid(allocator)) {
            return XGL_ERR_INVALID_PARAM;
        }
        storage_size = plan.requirements.size;
        storage = xgl_alloc(allocator, storage_size);
        if (storage == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
    } else {
        allocator = NULL;
    }

    error = prepare_storage(&plan, storage, storage_size);
    if (error != XGL_OK) {
        if (allocator != NULL) {
            xgl_free(allocator, storage);
        }
        return error;
    }

    xgl_workspace_t* workspace = storage;
    workspace->instance.config = config;
    workspace->instance.storage_allocator = allocator;
    *prepared = &workspace->instance;
    return XGL_OK;
}
