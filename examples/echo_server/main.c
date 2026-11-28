/**
 * \file            main.c
 * \brief           Deferred two-node echo with explicit time and static storage
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */
#include <stdio.h>

#include "../common/sim_link.h"

typedef struct {
    uint8_t pending[64];
    size_t length;
    uint16_t source;
    bool echo;
    unsigned received;
} echo_app_t;

/**
 * \brief           Copy borrowed data into a bounded slot without reentering
 * the instance
 * \details         Return BUSY while an earlier echo remains pending. Accept
 *                  only after copying the borrowed payload.
 */
static xgl_error_t echo_accept(xgl_handle_t handle, const xgl_rx_info_t* info,
                               const uint8_t* data, size_t length,
                               void* context) {
    (void)handle;
    echo_app_t* app = context;
    if (length > sizeof(app->pending)) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (app->echo && app->length != 0U) {
        return XGL_ERR_BUSY;
    }
    memcpy(app->pending, data, length);
    app->source = info->source_id;
    app->length = length;
    app->received++;
    return XGL_OK;
}

/** \brief           Run a bounded echo exchange and verify its application
 * result. */
int main(void) {
    static sim_node_t client, server;
    static sim_link_t link;
    sim_connect(&link);
    echo_app_t client_app = {0};
    echo_app_t server_app = {.echo = true};
    sim_prepare(&client, 1U);
    sim_prepare(&server, 2U);
    sim_route(&client, 0U, 2U, &link.phys[0]);
    sim_route(&server, 0U, 1U, &link.phys[1]);
    client.config.rx_callback = echo_accept;
    client.config.rx_user_data = &client_app;
    server.config.rx_callback = echo_accept;
    server.config.rx_user_data = &server_app;
    if (sim_init(&client) != XGL_OK || sim_init(&server) != XGL_OK) {
        return 1;
    }
    const uint8_t message[] = "deferred echo";
    xgl_tx_data_t request = {.target_id = 2U,
                             .data_type = 1U,
                             .data = message,
                             .data_len = sizeof(message),
                             .reliable = true};
    if (xgl_send_at(client.handle, &request, 0U) != XGL_OK) {
        return 2;
    }
    for (uint32_t now = 0; now < 1000U && client_app.received == 0U; ++now) {
        /* Callback work from a previous tick is sent outside the callback. */
        if (server_app.length != 0U) {
            xgl_tx_data_t response = {.target_id = server_app.source,
                                      .data_type = 1U,
                                      .data = server_app.pending,
                                      .data_len = server_app.length,
                                      .reliable = true};
            if (xgl_send_at(server.handle, &response, now) == XGL_OK) {
                server_app.length = 0U;
            }
        }
        if (sim_step(&client, now) != XGL_OK ||
            sim_step(&server, now) != XGL_OK) {
            return 3;
        }
    }
    bool complete = client_app.received == 1U &&
                    client_app.length == sizeof(message) &&
                    memcmp(client_app.pending, message, sizeof(message)) == 0;
    xgl_destroy(client.handle);
    xgl_destroy(server.handle);
    printf("Deferred echo: %s\n", complete ? "PASS" : "FAIL");
    return complete ? 0 : 4;
}
