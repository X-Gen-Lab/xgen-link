/**
 * \file            sim_link.h
 * \brief           Bounded synchronous host PHY and persistent static node
 * storage
 * \author          X-Gen Lab
 */
#ifndef XGL_EXAMPLE_SIM_LINK_H
#define XGL_EXAMPLE_SIM_LINK_H
#include <xgl/xgl.h>

#include <stdalign.h>
#include <string.h>

#define SIM_CHANNEL_CAPACITY   2048U
#define SIM_WORKSPACE_CAPACITY 16384U

typedef struct {
    uint8_t bytes[SIM_CHANNEL_CAPACITY];
    size_t head;
    size_t count;
} sim_channel_t;

typedef struct {
    sim_channel_t* input;
    sim_channel_t* output;
    bool drop_next_tx;
    unsigned dropped;
} sim_port_t;

typedef struct {
    xgl_config_t config;
    xgl_route_item_t routes[2];
    xgl_handle_t handle;
    alignas(xgm_max_align_t) uint8_t workspace[SIM_WORKSPACE_CAPACITY];
} sim_node_t;

/**
 * \brief           Copy all frame bytes before TX returns, optionally dropping
 * one frame
 * \param[in]       data: Borrowed complete frame
 * \param[in]       length: Number of bytes
 * \param[in,out]   context: Connected simulated port
 * \return          XGL_OK after copying or simulated loss, BUSY if ring is full
 */
static inline xgl_error_t sim_tx(const uint8_t* data, size_t length,
                                 void* context) {
    sim_port_t* port = context;
    if (port->drop_next_tx) {
        port->drop_next_tx = false;
        port->dropped++;
        return XGL_OK;
    }
    sim_channel_t* output = port->output;
    if (length > SIM_CHANNEL_CAPACITY - output->count) {
        return XGL_ERR_BUSY;
    }
    for (size_t i = 0; i < length; ++i) {
        output
            ->bytes[(output->head + output->count + i) % SIM_CHANNEL_CAPACITY] =
            data[i];
    }
    output->count += length;
    return XGL_OK;
}

/**
 * \brief           Read a bounded chunk while retaining unread bytes
 * \param[out]      data: Caller RX buffer
 * \param[in,out]   length: Capacity on entry, produced length on return
 * \param[in,out]   context: Connected simulated port
 * \return          XGL_OK
 */
static inline xgl_error_t sim_rx(uint8_t* data, size_t* length, void* context) {
    sim_channel_t* input = ((sim_port_t*)context)->input;
    if (*length > input->count) {
        *length = input->count;
    }
    for (size_t i = 0; i < *length; ++i) {
        data[i] = input->bytes[(input->head + i) % SIM_CHANNEL_CAPACITY];
    }
    input->head = (input->head + *length) % SIM_CHANNEL_CAPACITY;
    input->count -= *length;
    return XGL_OK;
}

/**
 * \brief           Prepare persistent configuration; set routes and callbacks
 * before init
 * \param[out]      node: Node whose address stays fixed until destroy
 * \param[in]       id: Local identifier
 */
static inline void sim_prepare(sim_node_t* node, uint16_t id) {
    memset(node, 0, sizeof(*node));
    xgl_config_t tiny = XGL_CONFIG_PRESET_TINY;
    node->config = tiny;
    node->config.source_id = id;
    node->config.protocol.window_size = 1U;
    node->config.protocol.ack_timeout_ms = 100U;
    node->config.features.max_peers = 1U;
    node->config.route_table = node->routes;
}

/**
 * \brief           Bind one persistent route before instance initialization
 * \param[in,out]   node: Prepared node
 * \param[in]       index: Route index, zero or one
 * \param[in]       target: Destination address
 * \param[in]       phy: Persistent synchronous PHY descriptor
 */
static inline void sim_route(sim_node_t* node, size_t index, uint16_t target,
                             xgl_phy_ops_t* phy) {
    node->routes[index] = (xgl_route_item_t){target, phy, 128U, 1000U, 1U};
    node->config.route_table_len = index + 1U;
}

/**
 * \brief           Initialize with an exact checked static storage requirement
 * \param[in,out]   node: Configured node and caller workspace
 * \return          XGL_OK on success, error code otherwise
 */
static inline xgl_error_t sim_init(sim_node_t* node) {
    xgl_memory_requirements_t required;
    xgl_error_t error = xgl_memory_requirements(&node->config, &required);
    if (error != XGL_OK) {
        return error;
    }
    if (required.size > sizeof(node->workspace)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    return xgl_init_static(&node->config, node->workspace, required.size,
                           &node->handle);
}

/**
 * \brief           Advance one node with caller time and a bounded RX budget
 * \param[in,out]   node: Initialized node
 * \param[in]       now_ms: Caller clock; ordinary unsigned wrap is supported
 * \return          XGL_OK on success, error code otherwise
 */
static inline xgl_error_t sim_step(sim_node_t* node, uint32_t now_ms) {
    const xgl_work_budget_t budget = {256U, 1000U};
    return xgl_step(node->handle, now_ms, &budget);
}
#endif
