/**
 * \file            main.c
 * \brief           Multi-node network example - demonstrates routing and packet forwarding
 * \author          X-Gen Lab
 * \date            2026-02-28
 *
 * \details         This example demonstrates:
 *                  - Three-node network topology (Node 1 -> Node 2 -> Node 3)
 *                  - Multiple protocol instances running simultaneously
 *                  - Packet routing through intermediate nodes
 *                  - Packet forwarding capabilities
 *                  - End-to-end communication across multi-hop network
 *                  - Statistics monitoring for each node
 */

#include <xgl/xgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Network Topology                                                          */
/*---------------------------------------------------------------------------*/

/**
 * Network Topology:
 *
 *   Node 1 (ID=1) <-----> Node 2 (ID=2) <-----> Node 3 (ID=3)
 *   [Source]              [Router]              [Destination]
 *
 * - Node 1 sends messages to Node 3
 * - Node 2 forwards packets between Node 1 and Node 3
 * - Node 3 receives messages and sends replies back to Node 1
 * - All communication goes through Node 2 (demonstrates routing)
 */

/*---------------------------------------------------------------------------*/
/* Simulated Physical Layer - Communication Channels                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Simulated communication channel between two nodes
 */
typedef struct {
    uint8_t* buffer;
    size_t buffer_size;
    size_t data_len;
    size_t read_pos;
    const char* name;
} sim_channel_t;

/* Communication channels */
static sim_channel_t channel_1_to_2;  /* Node 1 -> Node 2 */
static sim_channel_t channel_2_to_1;  /* Node 2 -> Node 1 */
static sim_channel_t channel_2_to_3;  /* Node 2 -> Node 3 */
static sim_channel_t channel_3_to_2;  /* Node 3 -> Node 2 */

/**
 * \brief           Initialize simulated channel
 */
static void sim_channel_init(sim_channel_t* channel, size_t buffer_size, const char* name)
{
    channel->buffer = (uint8_t*)malloc(buffer_size);
    channel->buffer_size = buffer_size;
    channel->data_len = 0;
    channel->read_pos = 0;
    channel->name = name;
}

/**
 * \brief           Free simulated channel
 */
static void sim_channel_free(sim_channel_t* channel)
{
    if (channel->buffer) {
        free(channel->buffer);
        channel->buffer = NULL;
    }
}

/**
 * \brief           Write to simulated channel
 */
static xgl_error_t sim_channel_write(sim_channel_t* channel,
                                     const uint8_t* data,
                                     size_t len)
{
    if (len > channel->buffer_size) {
        printf("[%s] Buffer overflow: %zu > %zu\n", channel->name, len, channel->buffer_size);
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(channel->buffer, data, len);
    channel->data_len = len;
    channel->read_pos = 0;

    return XGL_OK;
}

/**
 * \brief           Read from simulated channel
 */
static xgl_error_t sim_channel_read(sim_channel_t* channel,
                                    uint8_t* buffer,
                                    size_t* len)
{
    if (channel->read_pos >= channel->data_len) {
        *len = 0;
        return XGL_OK;
    }

    size_t available = channel->data_len - channel->read_pos;
    size_t to_read = (available < *len) ? available : *len;

    memcpy(buffer, channel->buffer + channel->read_pos, to_read);
    channel->read_pos += to_read;
    *len = to_read;

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Physical Layer Callbacks - Node 1                                         */
/*---------------------------------------------------------------------------*/

static xgl_error_t node1_phy_tx(const uint8_t* data, size_t len, void* user_data)
{
    (void)user_data;
    return sim_channel_write(&channel_1_to_2, data, len);
}

static xgl_error_t node1_phy_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    (void)user_data;
    return sim_channel_read(&channel_2_to_1, buffer, len);
}

/*---------------------------------------------------------------------------*/
/* Physical Layer Callbacks - Node 2 (Router)                                */
/*---------------------------------------------------------------------------*/

/* Node 2 has two physical interfaces: one to Node 1, one to Node 3 */

static xgl_error_t node2_phy_to_node1_tx(const uint8_t* data, size_t len, void* user_data)
{
    (void)user_data;
    return sim_channel_write(&channel_2_to_1, data, len);
}

static xgl_error_t node2_phy_to_node1_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    (void)user_data;
    return sim_channel_read(&channel_1_to_2, buffer, len);
}

static xgl_error_t node2_phy_to_node3_tx(const uint8_t* data, size_t len, void* user_data)
{
    (void)user_data;
    return sim_channel_write(&channel_2_to_3, data, len);
}

static xgl_error_t node2_phy_to_node3_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    (void)user_data;
    return sim_channel_read(&channel_3_to_2, buffer, len);
}

/*---------------------------------------------------------------------------*/
/* Physical Layer Callbacks - Node 3                                         */
/*---------------------------------------------------------------------------*/

static xgl_error_t node3_phy_tx(const uint8_t* data, size_t len, void* user_data)
{
    (void)user_data;
    return sim_channel_write(&channel_3_to_2, data, len);
}

static xgl_error_t node3_phy_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    (void)user_data;
    return sim_channel_read(&channel_2_to_3, buffer, len);
}

/*---------------------------------------------------------------------------*/
/* Protocol Callbacks - Node 1 (Source)                                      */
/*---------------------------------------------------------------------------*/

static void node1_on_receive(xgl_handle_t handle,
                             uint16_t source_id,
                             uint8_t data_type,
                             const uint8_t* data,
                             size_t len,
                             void* user_data)
{
    (void)handle;
    (void)user_data;

    printf("\n[NODE 1] Received reply from Node %d (type 0x%02X, %zu bytes)\n",
           source_id, data_type, len);

    /* Print received data */
    printf("[NODE 1] Data: \"");
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) {
            printf("%c", data[i]);
        } else {
            printf(".");
        }
    }
    printf("\"\n");
}

static void node1_on_error(xgl_handle_t handle,
                          xgl_error_t error,
                          const char* message,
                          void* user_data)
{
    (void)handle;
    (void)user_data;

    printf("[NODE 1 ERROR] Code %d: %s\n", error, message);
}

/*---------------------------------------------------------------------------*/
/* Protocol Callbacks - Node 2 (Router)                                      */
/*---------------------------------------------------------------------------*/

static void node2_on_receive(xgl_handle_t handle,
                             uint16_t source_id,
                             uint8_t data_type,
                             const uint8_t* data,
                             size_t len,
                             void* user_data)
{
    (void)handle;
    (void)data_type;
    (void)data;
    (void)user_data;

    /* Node 2 acts as a router - it forwards packets automatically */
    /* This callback is called for packets addressed to Node 2 itself */
    printf("[NODE 2] Received packet from Node %d (%zu bytes) - forwarding\n",
           source_id, len);
}

static void node2_on_error(xgl_handle_t handle,
                          xgl_error_t error,
                          const char* message,
                          void* user_data)
{
    (void)handle;
    (void)user_data;

    printf("[NODE 2 ERROR] Code %d: %s\n", error, message);
}

/*---------------------------------------------------------------------------*/
/* Protocol Callbacks - Node 3 (Destination)                                 */
/*---------------------------------------------------------------------------*/

static void node3_on_receive(xgl_handle_t handle,
                             uint16_t source_id,
                             uint8_t data_type,
                             const uint8_t* data,
                             size_t len,
                             void* user_data)
{
    (void)user_data;

    printf("\n[NODE 3] Received message from Node %d (type 0x%02X, %zu bytes)\n",
           source_id, data_type, len);

    /* Print received data */
    printf("[NODE 3] Data: \"");
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) {
            printf("%c", data[i]);
        } else {
            printf(".");
        }
    }
    printf("\"\n");

    /* Send reply back to Node 1 */
    const char* reply = "Reply from Node 3";
    xgl_tx_data_t tx_data = {
        .target_id = source_id,
        .data_type = 0x02,  /* Reply type */
        .data = (const uint8_t*)reply,
        .data_len = strlen(reply),
        .reliable = true,
        .priority = 0
    };

    xgl_error_t err = xgl_send(handle, &tx_data);
    if (err != XGL_OK) {
        printf("[NODE 3] Failed to send reply: %s\n", xgl_error_string(err));
    } else {
        printf("[NODE 3] Sent reply back to Node %d\n", source_id);
    }
}

static void node3_on_error(xgl_handle_t handle,
                          xgl_error_t error,
                          const char* message,
                          void* user_data)
{
    (void)handle;
    (void)user_data;

    printf("[NODE 3 ERROR] Code %d: %s\n", error, message);
}

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send message from Node 1 to Node 3
 */
static void send_message(xgl_handle_t node1, const char* message)
{
    printf("\n[NODE 1] Sending message to Node 3: \"%s\"\n", message);

    xgl_tx_data_t tx_data = {
        .target_id = 3,  /* Destination: Node 3 */
        .data_type = 0x01,
        .data = (const uint8_t*)message,
        .data_len = strlen(message),
        .reliable = true,
        .priority = 0
    };

    xgl_error_t err = xgl_send(node1, &tx_data);
    if (err != XGL_OK) {
        printf("[NODE 1] Failed to send message: %s\n", xgl_error_string(err));
    } else {
        printf("[NODE 1] Message sent successfully\n");
    }
}

/**
 * \brief           Print statistics for a node
 */
static void print_node_statistics(xgl_handle_t handle, const char* node_name)
{
    xgl_statistics_t stats;
    xgl_error_t err = xgl_stats_get(handle, &stats);

    if (err != XGL_OK) {
        printf("[%s STATS] Failed to get statistics: %s\n",
               node_name, xgl_error_string(err));
        return;
    }

    printf("\n[%s STATS] Protocol Statistics:\n", node_name);
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
    printf("  Performance:\n");
    printf("    Avg RTT:       %u ms\n", stats.avg_rtt_ms);
    printf("  Memory:\n");
    printf("    Memory Used:   %zu bytes\n", stats.memory_used);
    printf("    Memory Peak:   %zu bytes\n", stats.memory_peak);
}

/**
 * \brief           Print all network statistics
 */
static void print_network_statistics(xgl_handle_t node1,
                                     xgl_handle_t node2,
                                     xgl_handle_t node3)
{
    printf("\n=================================================\n");
    printf("  Network Statistics\n");
    printf("=================================================\n");

    print_node_statistics(node1, "NODE 1");
    print_node_statistics(node2, "NODE 2");
    print_node_statistics(node3, "NODE 3");

    printf("=================================================\n");
}

/*---------------------------------------------------------------------------*/
/* Main Function                                                             */
/*---------------------------------------------------------------------------*/

int main(void)
{
    printf("=================================================\n");
    printf("  xgen-link Multi-Node Network Example\n");
    printf("=================================================\n");
    printf("This example demonstrates a three-node network\n");
    printf("with routing and packet forwarding.\n");
    printf("\n");
    printf("Network Topology:\n");
    printf("  Node 1 <-----> Node 2 <-----> Node 3\n");
    printf("  [Source]       [Router]       [Destination]\n");
    printf("\n");
    printf("Node 1 sends messages to Node 3 through Node 2.\n");
    printf("Node 2 forwards packets between Node 1 and Node 3.\n");
    printf("Node 3 receives messages and sends replies back.\n");
    printf("=================================================\n\n");

    /* Initialize simulated channels */
    printf("[INIT] Initializing communication channels...\n");
    sim_channel_init(&channel_1_to_2, 2048, "CH_1->2");
    sim_channel_init(&channel_2_to_1, 2048, "CH_2->1");
    sim_channel_init(&channel_2_to_3, 2048, "CH_2->3");
    sim_channel_init(&channel_3_to_2, 2048, "CH_3->2");
    printf("[INIT] Channels initialized\n");

    /* Setup physical layer operations for Node 1 */
    xgl_phy_ops_t node1_phy = {
        .tx = node1_phy_tx,
        .rx = node1_phy_rx,
        .user_data = NULL
    };

    /* Setup physical layer operations for Node 2 (two interfaces) */
    xgl_phy_ops_t node2_phy_to_node1 = {
        .tx = node2_phy_to_node1_tx,
        .rx = node2_phy_to_node1_rx,
        .user_data = NULL
    };

    xgl_phy_ops_t node2_phy_to_node3 = {
        .tx = node2_phy_to_node3_tx,
        .rx = node2_phy_to_node3_rx,
        .user_data = NULL
    };

    /* Setup physical layer operations for Node 3 */
    xgl_phy_ops_t node3_phy = {
        .tx = node3_phy_tx,
        .rx = node3_phy_rx,
        .user_data = NULL
    };

    /* Setup route table for Node 1 */
    /* Node 1 routes to Node 2 and Node 3 (both via Node 2) */
    xgl_route_item_t node1_routes[] = {
        {
            .target_id = 2,
            .phy = &node1_phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        },
        {
            .target_id = 3,
            .phy = &node1_phy,  /* Route to Node 3 via same PHY (Node 2 will forward) */
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        }
    };

    /* Setup route table for Node 2 (Router) */
    /* Node 2 routes to Node 1 and Node 3 via different PHY interfaces */
    xgl_route_item_t node2_routes[] = {
        {
            .target_id = 1,
            .phy = &node2_phy_to_node1,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        },
        {
            .target_id = 3,
            .phy = &node2_phy_to_node3,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        }
    };

    /* Setup route table for Node 3 */
    /* Node 3 routes to Node 1 and Node 2 (both via Node 2) */
    xgl_route_item_t node3_routes[] = {
        {
            .target_id = 2,
            .phy = &node3_phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        },
        {
            .target_id = 1,
            .phy = &node3_phy,  /* Route to Node 1 via same PHY (Node 2 will forward) */
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        }
    };

    /* Configure Node 1 */
    printf("[INIT] Configuring Node 1...\n");
    xgl_config_t node1_config;
    xgl_config_get_default(&node1_config);
    node1_config.name = "node1";
    node1_config.source_id = 1;
    node1_config.route_table = node1_routes;
    node1_config.route_table_len = 2;
    node1_config.rx_callback = node1_on_receive;
    node1_config.error_callback = node1_on_error;
    node1_config.callback_user_data = NULL;

    /* Configure Node 2 (Router) */
    printf("[INIT] Configuring Node 2 (Router)...\n");
    xgl_config_t node2_config;
    xgl_config_get_default(&node2_config);
    node2_config.name = "node2";
    node2_config.source_id = 2;
    node2_config.route_table = node2_routes;
    node2_config.route_table_len = 2;
    node2_config.rx_callback = node2_on_receive;
    node2_config.error_callback = node2_on_error;
    node2_config.callback_user_data = NULL;

    /* Configure Node 3 */
    printf("[INIT] Configuring Node 3...\n");
    xgl_config_t node3_config;
    xgl_config_get_default(&node3_config);
    node3_config.name = "node3";
    node3_config.source_id = 3;
    node3_config.route_table = node3_routes;
    node3_config.route_table_len = 2;
    node3_config.rx_callback = node3_on_receive;
    node3_config.error_callback = node3_on_error;
    node3_config.callback_user_data = NULL;

    /* Create protocol instances */
    printf("[INIT] Creating Node 1 instance...\n");
    xgl_handle_t node1 = xgl_create(&node1_config);
    if (node1 == NULL) {
        printf("[ERROR] Failed to create Node 1 instance\n");
        goto cleanup;
    }

    printf("[INIT] Creating Node 2 instance...\n");
    xgl_handle_t node2 = xgl_create(&node2_config);
    if (node2 == NULL) {
        printf("[ERROR] Failed to create Node 2 instance\n");
        xgl_destroy(node1);
        goto cleanup;
    }

    printf("[INIT] Creating Node 3 instance...\n");
    xgl_handle_t node3 = xgl_create(&node3_config);
    if (node3 == NULL) {
        printf("[ERROR] Failed to create Node 3 instance\n");
        xgl_destroy(node1);
        xgl_destroy(node2);
        goto cleanup;
    }

    /* Initialize instances */
    printf("[INIT] Initializing Node 1...\n");
    if (xgl_init(node1) != XGL_OK) {
        printf("[ERROR] Failed to initialize Node 1\n");
        xgl_destroy(node1);
        xgl_destroy(node2);
        xgl_destroy(node3);
        goto cleanup;
    }

    printf("[INIT] Initializing Node 2...\n");
    if (xgl_init(node2) != XGL_OK) {
        printf("[ERROR] Failed to initialize Node 2\n");
        xgl_destroy(node1);
        xgl_destroy(node2);
        xgl_destroy(node3);
        goto cleanup;
    }

    printf("[INIT] Initializing Node 3...\n");
    if (xgl_init(node3) != XGL_OK) {
        printf("[ERROR] Failed to initialize Node 3\n");
        xgl_destroy(node1);
        xgl_destroy(node2);
        xgl_destroy(node3);
        goto cleanup;
    }

    printf("[INIT] All nodes initialized successfully\n\n");

    /* Print initial statistics */
    print_network_statistics(node1, node2, node3);

    /* Demonstrate multi-hop communication */
    printf("\n=================================================\n");
    printf("  Demonstrating Multi-Hop Communication\n");
    printf("=================================================\n");

    /* Send messages from Node 1 to Node 3 */
    send_message(node1, "Hello from Node 1!");
    send_message(node1, "Testing multi-hop routing");
    send_message(node1, "xgen-link protocol rocks!");

    /* Run protocol processing to complete communication */
    printf("\n[MAIN] Processing protocol messages...\n");
    int iterations = 0;
    const int max_iterations = 200;

    while (iterations < max_iterations) {
        /* Process all nodes at 100 Hz */
        xgl_run(node1, 100);
        xgl_run(node2, 100);
        xgl_run(node3, 100);

        iterations++;

        /* Progress indicator */
        if (iterations % 20 == 0) {
            printf(".");
            fflush(stdout);
        }
    }
    printf("\n");

    printf("[MAIN] Protocol processing complete\n");

    /* Print final statistics */
    print_network_statistics(node1, node2, node3);

    /* Analyze routing behavior */
    printf("\n=================================================\n");
    printf("  Routing Analysis\n");
    printf("=================================================\n");
    printf("Expected behavior:\n");
    printf("  - Node 1 sends packets to Node 3\n");
    printf("  - Node 2 receives and forwards packets\n");
    printf("  - Node 3 receives packets and sends replies\n");
    printf("  - Node 2 forwards replies back to Node 1\n");
    printf("  - Node 1 receives replies from Node 3\n");
    printf("\n");
    printf("Check the statistics above to verify:\n");
    printf("  - Node 1: TX packets = messages sent, RX packets = replies received\n");
    printf("  - Node 2: TX + RX packets = forwarded traffic (should be highest)\n");
    printf("  - Node 3: RX packets = messages received, TX packets = replies sent\n");
    printf("=================================================\n");

    /* Cleanup */
    printf("\n[CLEANUP] Destroying protocol instances...\n");
    xgl_destroy(node1);
    xgl_destroy(node2);
    xgl_destroy(node3);

cleanup:
    /* Free resources */
    sim_channel_free(&channel_1_to_2);
    sim_channel_free(&channel_2_to_1);
    sim_channel_free(&channel_2_to_3);
    sim_channel_free(&channel_3_to_2);

    printf("[CLEANUP] Done\n");

    printf("\n=================================================\n");
    printf("  Multi-Node Network Example Complete\n");
    printf("=================================================\n");

    return 0;
}
