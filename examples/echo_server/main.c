/**
 * \file            main.c
 * \brief           Two-node echo example
 * \author          Nexus Team
 * \date            2026-02-28
 *
 * \details         This example demonstrates:
 *                  - Creating two protocol instances
 *                  - Connecting them through simulated point-to-point PHY links
 *                  - Receiving application data through callbacks
 *                  - Echoing received data back to the source node
 *                  - Collecting statistics from both nodes
 */

#include <xgl/xgl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Simulated Physical Layer                                                  */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint8_t data[2048];
    size_t write_pos;
    size_t read_pos;
} sim_channel_t;

typedef struct {
    const char* name;
    sim_channel_t* tx;
    sim_channel_t* rx;
} sim_phy_ctx_t;

typedef struct {
    const char* name;
    bool echo_enabled;
    int received_count;
    int echoed_count;
    int error_count;
} echo_app_ctx_t;

static sim_channel_t channel_1_to_2;
static sim_channel_t channel_2_to_1;

static xgl_error_t sim_channel_write(sim_channel_t* channel,
                                     const uint8_t* data,
                                     size_t len)
{
    if (channel == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (channel->write_pos + len > sizeof(channel->data)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(channel->data + channel->write_pos, data, len);
    channel->write_pos += len;
    return XGL_OK;
}

static xgl_error_t sim_channel_read(sim_channel_t* channel,
                                    uint8_t* buffer,
                                    size_t* len)
{
    if (channel == NULL || buffer == NULL || len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (channel->read_pos >= channel->write_pos) {
        *len = 0;
        return XGL_OK;
    }

    const size_t available = channel->write_pos - channel->read_pos;
    const size_t to_read = (available < *len) ? available : *len;

    memcpy(buffer, channel->data + channel->read_pos, to_read);
    channel->read_pos += to_read;
    *len = to_read;

    if (channel->read_pos >= channel->write_pos) {
        channel->read_pos = 0;
        channel->write_pos = 0;
    }

    return XGL_OK;
}

static xgl_error_t phy_tx(const uint8_t* data, size_t len, void* user_data)
{
    sim_phy_ctx_t* phy = (sim_phy_ctx_t*)user_data;
    if (phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    const xgl_error_t err = sim_channel_write(phy->tx, data, len);
    if (err != XGL_OK) {
        printf("[PHY:%s] TX failed: %s\n", phy->name, xgl_error_string(err));
        return err;
    }

    printf("[PHY:%s] Transmitted %zu bytes\n", phy->name, len);
    return XGL_OK;
}

static xgl_error_t phy_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    sim_phy_ctx_t* phy = (sim_phy_ctx_t*)user_data;
    if (phy == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    const xgl_error_t err = sim_channel_read(phy->rx, buffer, len);
    if (err != XGL_OK) {
        printf("[PHY:%s] RX failed: %s\n", phy->name, xgl_error_string(err));
        return err;
    }

    if (*len > 0) {
        printf("[PHY:%s] Received %zu bytes\n", phy->name, *len);
    }

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Protocol Callbacks                                                        */
/*---------------------------------------------------------------------------*/

static void on_receive(xgl_handle_t handle,
                       uint16_t source_id,
                       uint8_t data_type,
                       const uint8_t* data,
                       size_t len,
                       void* user_data)
{
    echo_app_ctx_t* app = (echo_app_ctx_t*)user_data;
    if (app == NULL) {
        return;
    }

    printf("\n[%s] Received %zu bytes from node %u (type 0x%02X)\n",
           app->name,
           len,
           source_id,
           data_type);

    app->received_count++;
    printf("[%s] Data: \"", app->name);
    for (size_t i = 0; i < len; i++) {
        const uint8_t ch = data[i];
        printf("%c", (ch >= 32U && ch <= 126U) ? (char)ch : '.');
    }
    printf("\"\n");

    if (!app->echo_enabled) {
        return;
    }

    xgl_tx_data_t tx_data = {
        .target_id = source_id,
        .data_type = data_type,
        .data = data,
        .data_len = len,
        .reliable = false,
        .priority = 0
    };

    const xgl_error_t err = xgl_send(handle, &tx_data);
    if (err != XGL_OK) {
        app->error_count++;
        printf("[%s] Echo failed: %s\n", app->name, xgl_error_string(err));
        return;
    }

    app->echoed_count++;
    printf("[%s] Echoed %zu bytes back to node %u\n",
           app->name,
           len,
           source_id);
}

static void on_error(xgl_handle_t handle,
                     xgl_error_t error,
                     const char* message,
                     void* user_data)
{
    (void)handle;

    echo_app_ctx_t* app = (echo_app_ctx_t*)user_data;
    if (app != NULL) {
        app->error_count++;
        printf("[%s][ERROR] Code %d: %s\n", app->name, error, message);
    } else {
        printf("[ERROR] Code %d: %s\n", error, message);
    }
}

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

static void send_test_message(xgl_handle_t handle,
                              uint16_t target_id,
                              const char* message)
{
    printf("\n[CLIENT] Sending message to node %u: \"%s\"\n", target_id, message);

    xgl_tx_data_t tx_data = {
        .target_id = target_id,
        .data_type = 0x01,
        .data = (const uint8_t*)message,
        .data_len = strlen(message),
        .reliable = false,
        .priority = 0
    };

    const xgl_error_t err = xgl_send(handle, &tx_data);
    if (err != XGL_OK) {
        printf("[CLIENT] Send failed: %s\n", xgl_error_string(err));
    } else {
        printf("[CLIENT] Message sent successfully\n");
    }
}

static void pump_protocol(xgl_handle_t client, xgl_handle_t server, int rounds)
{
    for (int i = 0; i < rounds; i++) {
        (void)xgl_run(client, 100);
        (void)xgl_run(server, 100);
        (void)xgl_run(client, 100);
        (void)xgl_run(server, 100);
    }
}

static void print_statistics(const char* name, xgl_handle_t handle)
{
    xgl_statistics_t stats;
    const xgl_error_t err = xgl_stats_get(handle, &stats);

    if (err != XGL_OK) {
        printf("[STATS:%s] Failed to get statistics: %s\n",
               name,
               xgl_error_string(err));
        return;
    }

    printf("\n[STATS:%s] Protocol Statistics:\n", name);
    printf("  Data Link Layer:\n");
    printf("    TX Packets:    %llu\n", (unsigned long long)stats.datalink.tx_packets);
    printf("    TX Bytes:      %llu\n", (unsigned long long)stats.datalink.tx_bytes);
    printf("    TX Errors:     %llu\n", (unsigned long long)stats.datalink.tx_errors);
    printf("    RX Packets:    %llu\n", (unsigned long long)stats.datalink.rx_packets);
    printf("    RX Bytes:      %llu\n", (unsigned long long)stats.datalink.rx_bytes);
    printf("    RX Errors:     %llu\n", (unsigned long long)stats.datalink.rx_errors);
    printf("  Transport Layer:\n");
    printf("    TX Retries:    %llu\n", (unsigned long long)stats.tx_retries);
    printf("  CRC Errors:\n");
    printf("    CRC16 Errors:  %llu\n", (unsigned long long)stats.rx_crc16_errors);
    printf("  Runtime:\n");
    printf("    Avg RTT:       %u ms\n", stats.avg_rtt_ms);
    printf("    Memory Used:   %zu bytes\n", stats.memory_used);
    printf("    Memory Peak:   %zu bytes\n", stats.memory_peak);
}

/*---------------------------------------------------------------------------*/
/* Main Function                                                             */
/*---------------------------------------------------------------------------*/

int main(void)
{
    printf("=================================================\n");
    printf("  xgen-link Two-Node Echo Example\n");
    printf("=================================================\n");
    printf("This example demonstrates application\n");
    printf("delivery between two MCU-style protocol nodes.\n");
    printf("=================================================\n\n");

    sim_phy_ctx_t node1_phy_ctx = {
        .name = "node1",
        .tx = &channel_1_to_2,
        .rx = &channel_2_to_1
    };
    sim_phy_ctx_t node2_phy_ctx = {
        .name = "node2",
        .tx = &channel_2_to_1,
        .rx = &channel_1_to_2
    };

    xgl_phy_ops_t node1_phy = {
        .tx = phy_tx,
        .rx = phy_rx,
        .user_data = &node1_phy_ctx
    };
    xgl_phy_ops_t node2_phy = {
        .tx = phy_tx,
        .rx = phy_rx,
        .user_data = &node2_phy_ctx
    };

    xgl_route_item_t node1_routes[] = {
        {
            .target_id = 2,
            .phy = &node1_phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        }
    };
    xgl_route_item_t node2_routes[] = {
        {
            .target_id = 1,
            .phy = &node2_phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        }
    };

    echo_app_ctx_t server_app = {
        .name = "SERVER",
        .echo_enabled = true
    };
    echo_app_ctx_t client_app = {
        .name = "CLIENT",
        .echo_enabled = false
    };

    xgl_config_t server_config;
    xgl_config_t client_config;
    xgl_config_get_default(&server_config);
    xgl_config_get_default(&client_config);

    server_config.name = "echo_server";
    server_config.source_id = 1;
    server_config.route_table = node1_routes;
    server_config.route_table_len = 1;
    server_config.rx_callback = on_receive;
    server_config.error_callback = on_error;
    server_config.callback_user_data = &server_app;

    client_config.name = "echo_client";
    client_config.source_id = 2;
    client_config.route_table = node2_routes;
    client_config.route_table_len = 1;
    client_config.rx_callback = on_receive;
    client_config.error_callback = on_error;
    client_config.callback_user_data = &client_app;

    printf("[INIT] Creating protocol instances...\n");
    xgl_handle_t server = xgl_create(&server_config);
    xgl_handle_t client = xgl_create(&client_config);
    if (server == NULL || client == NULL) {
        printf("[ERROR] Failed to create protocol instances\n");
        xgl_destroy(server);
        xgl_destroy(client);
        return 1;
    }

    printf("[INIT] Initializing protocol instances...\n");
    xgl_error_t err = xgl_init(server);
    if (err != XGL_OK) {
        printf("[ERROR] Failed to initialize server: %s\n", xgl_error_string(err));
        xgl_destroy(server);
        xgl_destroy(client);
        return 1;
    }

    err = xgl_init(client);
    if (err != XGL_OK) {
        printf("[ERROR] Failed to initialize client: %s\n", xgl_error_string(err));
        xgl_destroy(server);
        xgl_destroy(client);
        return 1;
    }

    print_statistics("SERVER", server);
    print_statistics("CLIENT", client);

    printf("\n=================================================\n");
    printf("  Testing Echo Operations (Full Protocol Stack)\n");
    printf("=================================================\n");

    send_test_message(client, 1, "Hello, Echo Server!");
    pump_protocol(client, server, 10);

    send_test_message(client, 1, "Testing 1-2-3");
    pump_protocol(client, server, 10);

    send_test_message(client, 1, "xgen-link protocol");
    pump_protocol(client, server, 10);

    print_statistics("SERVER", server);
    print_statistics("CLIENT", client);

    const bool passed = (server_app.error_count == 0) &&
                        (client_app.error_count == 0) &&
                        (server_app.received_count >= 3) &&
                        (server_app.echoed_count >= 3) &&
                        (client_app.received_count >= 3);

    if (!passed) {
        printf("[ACCEPTANCE] FAILED: server_rx=%d server_echo=%d "
               "client_rx=%d server_errors=%d client_errors=%d\n",
               server_app.received_count,
               server_app.echoed_count,
               client_app.received_count,
               server_app.error_count,
               client_app.error_count);
        xgl_destroy(server);
        xgl_destroy(client);
        return 1;
    }

    printf("[ACCEPTANCE] PASSED: server_rx=%d server_echo=%d client_rx=%d\n",
           server_app.received_count,
           server_app.echoed_count,
           client_app.received_count);

    printf("\n[CLEANUP] Destroying protocol instances...\n");
    xgl_destroy(server);
    xgl_destroy(client);
    printf("[CLEANUP] Done\n");

    printf("\n=================================================\n");
    printf("  Echo Example Complete\n");
    printf("=================================================\n");

    return 0;
}
