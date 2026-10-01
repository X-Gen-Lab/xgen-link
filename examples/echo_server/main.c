/**
 * \file            main.c
 * \brief           Deferred two-node echo with explicit time and static storage
 * \author          X-Gen Lab
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
 * \param[in]       handle: Callback instance, deliberately unused
 * \param[in]       source: Remote node
 * \param[in]       type: Application type
 * \param[in]       data: Borrowed payload
 * \param[in]       length: Payload size
 * \param[in,out]   context: Pending application slot
 * \return          XGL_OK after copying, BUSY if an echo is still pending
 */
static xgl_error_t echo_accept(xgl_handle_t handle, uint16_t source,
                               uint8_t type, const uint8_t* data, size_t length,
                               void* context) {
    (void)handle;
    (void)type;
    echo_app_t* app = context;
    if (length > sizeof(app->pending)) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (app->echo && app->length != 0U) {
        return XGL_ERR_BUSY;
    }
    memcpy(app->pending, data, length);
    app->source = source;
    app->length = length;
    app->received++;
    return XGL_OK;
}

/** \brief           Run a bounded echo exchange and verify its application
 * result. */
int main(void) {
    static sim_node_t client, server;
    static sim_channel_t to_client, to_server;
    sim_port_t client_port = {&to_client, &to_server, false, 0};
    sim_port_t server_port = {&to_server, &to_client, false, 0};
    xgl_phy_ops_t client_phy = {sim_tx, sim_rx, &client_port};
    xgl_phy_ops_t server_phy = {sim_tx, sim_rx, &server_port};
    echo_app_t client_app = {0};
    echo_app_t server_app = {.echo = true};
    sim_prepare(&client, 1U);
    sim_prepare(&server, 2U);
    sim_route(&client, 0U, 2U, &client_phy);
    sim_route(&server, 0U, 1U, &server_phy);
    client.config.rx_accept_callback = echo_accept;
    client.config.callback_user_data = &client_app;
    server.config.rx_accept_callback = echo_accept;
    server.config.callback_user_data = &server_app;
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
