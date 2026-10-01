/**
 * \file            static_workspace_smoke.c
 * \brief           Public API static-workspace lifecycle and backpressure test
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

#include <stdalign.h>
#include <string.h>

typedef struct smoke_endpoint_s {
    struct smoke_endpoint_s* remote;
    uint8_t incoming[256];
    size_t incoming_len;
    unsigned deliveries;
    bool busy;
    bool drop;
} smoke_endpoint_t;

/**
 * \brief           Synchronously copy a frame into the opposite endpoint.
 */
static xgl_error_t smoke_tx(const uint8_t* data, size_t len, void* ctx) {
    smoke_endpoint_t* endpoint = ctx;
    if (endpoint->drop) {
        endpoint->drop = false;
        return XGL_OK;
    }
    smoke_endpoint_t* remote = endpoint->remote;
    if (len > sizeof(remote->incoming) - remote->incoming_len) {
        return XGL_ERR_BUSY;
    }
    memcpy(remote->incoming + remote->incoming_len, data, len);
    remote->incoming_len += len;
    return XGL_OK;
}

/**
 * \brief           Preserve bytes not consumed by a partial PHY read.
 */
static xgl_error_t smoke_rx(uint8_t* data, size_t* len, void* ctx) {
    smoke_endpoint_t* endpoint = ctx;
    if (*len > endpoint->incoming_len) {
        *len = endpoint->incoming_len;
    }
    memcpy(data, endpoint->incoming, *len);
    endpoint->incoming_len -= *len;
    memmove(endpoint->incoming, endpoint->incoming + *len,
            endpoint->incoming_len);
    return XGL_OK;
}

/**
 * \brief           Model a boot application with a busy flash staging slot.
 */
static xgl_error_t smoke_accept(xgl_handle_t handle, uint16_t source_id,
                                uint8_t type, const uint8_t* data, size_t len,
                                void* ctx) {
    (void)handle;
    (void)source_id;
    smoke_endpoint_t* endpoint = ctx;
    if (endpoint->busy) {
        return XGL_ERR_BUSY;
    }
    if (type != 7U || len != 3U || memcmp(data, "abc", 3U) != 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    ++endpoint->deliveries;
    return XGL_OK;
}

/**
 * \brief           Exercise both instances without calling a system allocator.
 */
int main(void) {
    static alignas(xgm_max_align_t) uint8_t storage_a[16384];
    static alignas(xgm_max_align_t) uint8_t storage_b[16384];
    smoke_endpoint_t a = {0};
    smoke_endpoint_t b = {0};
    a.remote = &b;
    b.remote = &a;
    xgl_phy_ops_t phy_a = {smoke_tx, smoke_rx, &a};
    xgl_phy_ops_t phy_b = {smoke_tx, smoke_rx, &b};
    xgl_route_item_t route_a = {2U, &phy_a, 128U, 1000U, 0U};
    xgl_route_item_t route_b = {1U, &phy_b, 128U, 1000U, 0U};
    xgl_config_t config_a = XGL_CONFIG_PRESET_TINY;
    config_a.protocol.window_size = 1U;
    config_a.features.max_peers = 1U;
    config_a.route_table = &route_a;
    config_a.route_table_len = 1U;
    config_a.rx_accept_callback = smoke_accept;
    config_a.callback_user_data = &a;
    xgl_config_t config_b = config_a;
    config_b.source_id = 2U;
    config_b.route_table = &route_b;
    config_b.callback_user_data = &b;
    xgl_memory_requirements_t required;
    if (xgl_memory_requirements(&config_a, &required) != XGL_OK ||
        required.size > sizeof(storage_a)) {
        return 1;
    }
    xgl_handle_t ha = NULL;
    xgl_handle_t hb = NULL;
    memset(storage_a, 0xA5, sizeof(storage_a));
    if (xgl_init_static(&config_a, storage_a, required.size - 1U, &ha) !=
            XGL_ERR_BUFFER_TOO_SMALL ||
        ha != NULL || storage_a[0] != 0xA5U) {
        return 2;
    }
    if (xgl_init_static(&config_a, storage_a + 1U, required.size, &ha) !=
            XGL_ERR_INVALID_PARAM ||
        ha != NULL) {
        return 3;
    }
    const xgl_work_budget_t budget = {256U, 1000U};
    const uint8_t payload[] = {'a', 'b', 'c'};
    const xgl_tx_data_t tx = {.target_id = 2U,
                              .data_type = 7U,
                              .data = payload,
                              .data_len = sizeof(payload),
                              .reliable = true,
                              .timeout_ms = 100U};
    for (unsigned cycle = 0U; cycle < 3U; ++cycle) {
        const uint32_t start = cycle == 2U ? UINT32_MAX - 50U : 0U;
        if (xgl_init_static(&config_a, storage_a, required.size, &ha) !=
                XGL_OK ||
            xgl_init_static(&config_b, storage_b, required.size, &hb) !=
                XGL_OK) {
            return 4;
        }
        b.busy = true;
        if (xgl_send_at(ha, &tx, start) != XGL_OK ||
            xgl_send_at(ha, &tx, start) != XGL_ERR_WINDOW_FULL) {
            return 5;
        }
        (void)xgl_step(hb, start, &budget);
        if (a.incoming_len != 0U || b.deliveries != cycle * 2U) {
            return 6;
        }
        b.busy = false;
        (void)xgl_step(ha, start + 100U, &budget);
        (void)xgl_step(hb, start + 100U, &budget);
        (void)xgl_step(ha, start + 101U, &budget);
        if (b.deliveries != cycle * 2U + 1U) {
            return 7;
        }
        /* Drop the next DATA and verify the retained slot is reused after ACK.
         */
        a.drop = true;
        if (xgl_send_at(ha, &tx, start + 200U) != XGL_OK) {
            return 8;
        }
        (void)xgl_step(ha, start + 300U, &budget);
        (void)xgl_step(hb, start + 300U, &budget);
        (void)xgl_step(ha, start + 301U, &budget);
        if (b.deliveries != cycle * 2U + 2U) {
            return 9;
        }
        /* Reuse the same peer state for reliable traffic in the other
         * direction. */
        xgl_tx_data_t reverse = tx;
        reverse.target_id = 1U;
        if (xgl_send_at(hb, &reverse, start + 400U) != XGL_OK) {
            return 10;
        }
        (void)xgl_step(ha, start + 400U, &budget);
        (void)xgl_step(hb, start + 401U, &budget);
        if (a.deliveries != cycle + 1U) {
            return 11;
        }
        xgl_destroy(ha);
        xgl_destroy(hb);
    }
    return 0;
}
