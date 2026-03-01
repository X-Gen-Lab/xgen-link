/**
 * \file            main.c
 * \brief           Echo server example - demonstrates basic send/receive
 * \author          Nexus Team
 * \date            2026-02-28
 *
 * \details         This example demonstrates:
 *                  - Creating and initializing a protocol instance
 *                  - Setting up physical layer callbacks (simulated)
 *                  - Receiving data via callback
 *                  - Echoing received data back to sender
 *                  - Basic error handling
 *                  - Statistics monitoring
 */

#include <xgl/xgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Simulated Physical Layer                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Simulated TX/RX buffers for loopback testing
 */
static uint8_t sim_loopback_buffer[2048];
static size_t sim_loopback_write_pos = 0;
static size_t sim_loopback_read_pos = 0;

/**
 * \brief           Physical layer TX callback (simulated with loopback)
 * \param[in]       data: Data to transmit
 * \param[in]       len: Data length
 * \param[in]       user_data: User data (unused)
 * \return          XGL_OK on success
 */
static xgl_error_t phy_tx(const uint8_t* data, size_t len, void* user_data)
{
    (void)user_data;
    
    if (sim_loopback_write_pos + len > sizeof(sim_loopback_buffer)) {
        printf("[PHY] TX buffer overflow\n");
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Copy data to loopback buffer */
    memcpy(sim_loopback_buffer + sim_loopback_write_pos, data, len);
    sim_loopback_write_pos += len;
    
    printf("[PHY] Transmitted %zu bytes (total buffered: %zu)\n", len, sim_loopback_write_pos - sim_loopback_read_pos);
    return XGL_OK;
}

/**
 * \brief           Physical layer RX callback (simulated with loopback)
 * \param[out]      buffer: Buffer to receive data
 * \param[in,out]   len: Buffer size on input, received length on output
 * \param[in]       user_data: User data (unused)
 * \return          XGL_OK on success
 */
static xgl_error_t phy_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    (void)user_data;
    
    if (sim_loopback_read_pos >= sim_loopback_write_pos) {
        /* No data available */
        *len = 0;
        return XGL_OK;
    }
    
    size_t available = sim_loopback_write_pos - sim_loopback_read_pos;
    size_t to_read = (available < *len) ? available : *len;
    
    memcpy(buffer, sim_loopback_buffer + sim_loopback_read_pos, to_read);
    sim_loopback_read_pos += to_read;
    *len = to_read;
    
    printf("[PHY] Received %zu bytes (remaining: %zu)\n", to_read, sim_loopback_write_pos - sim_loopback_read_pos);
    
    /* Reset buffer when fully consumed */
    if (sim_loopback_read_pos >= sim_loopback_write_pos) {
        sim_loopback_read_pos = 0;
        sim_loopback_write_pos = 0;
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Protocol Callbacks                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Echo tracking - prevents infinite echo loops
 */
static int echo_depth = 0;
static const int MAX_ECHO_DEPTH = 1;  /* Only echo once, don't echo echoes */

/**
 * \brief           Receive callback - echoes data back to sender
 * \param[in]       handle: Protocol instance handle
 * \param[in]       source_id: Source node ID
 * \param[in]       data_type: Data type
 * \param[in]       data: Received data
 * \param[in]       len: Data length
 * \param[in]       user_data: User data (protocol handle)
 */
static void on_receive(xgl_handle_t handle,
                      uint8_t source_id,
                      uint8_t data_type,
                      const uint8_t* data,
                      size_t len,
                      void* user_data)
{
    /* Get handle from user_data if handle is NULL */
    if (handle == NULL && user_data != NULL) {
        handle = (xgl_handle_t)user_data;
    }
    
    printf("\n[ECHO] Received %zu bytes from node %d (type 0x%02X)\n", 
           len, source_id, data_type);
    
    /* Print received data as string if printable */
    printf("[ECHO] Data: \"");
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) {
            printf("%c", data[i]);
        } else {
            printf(".");
        }
    }
    printf("\"\n");
    
    /* Check echo depth to prevent infinite loops */
    if (echo_depth >= MAX_ECHO_DEPTH) {
        printf("[ECHO] Max echo depth reached, not echoing back (prevents infinite loop)\n");
        echo_depth = 0;  /* Reset for next message */
        return;
    }
    
    /* Increment echo depth */
    echo_depth++;
    
    /* Echo data back to sender */
    xgl_tx_data_t tx_data = {
        .target_id = source_id,
        .data_type = data_type,
        .data = data,
        .data_len = len,
        .reliable = true,
        .priority = 0
    };
    
    xgl_error_t err = xgl_send(handle, &tx_data);
    if (err != XGL_OK) {
        printf("[ECHO] Failed to echo data: %s\n", xgl_error_string(err));
        echo_depth--;  /* Decrement on failure */
    } else {
        printf("[ECHO] Echoed %zu bytes back to node %d (depth: %d)\n", len, source_id, echo_depth);
    }
}

/**
 * \brief           Error callback - logs errors
 * \param[in]       handle: Protocol instance handle
 * \param[in]       error: Error code
 * \param[in]       message: Error message
 * \param[in]       user_data: User data (unused)
 */
static void on_error(xgl_handle_t handle,
                    xgl_error_t error,
                    const char* message,
                    void* user_data)
{
    (void)handle;
    (void)user_data;
    
    printf("[ERROR] Code %d: %s\n", error, message);
}

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send a test message to the echo server
 * \param[in]       handle: Protocol instance handle
 * \param[in]       target_id: Target node ID
 * \param[in]       message: Message string
 */
static void send_test_message(xgl_handle_t handle, uint8_t target_id, const char* message)
{
    printf("\n[TEST] Sending message to node %d: \"%s\"\n", target_id, message);
    
    /* Reset echo depth for new message */
    echo_depth = 0;
    
    xgl_tx_data_t tx_data = {
        .target_id = target_id,
        .data_type = 0x01,
        .data = (const uint8_t*)message,
        .data_len = strlen(message),
        .reliable = true,
        .priority = 0
    };
    
    xgl_error_t err = xgl_send(handle, &tx_data);
    if (err != XGL_OK) {
        printf("[TEST] Failed to send message: %s\n", xgl_error_string(err));
    } else {
        printf("[TEST] Message sent successfully\n");
    }
}

/**
 * \brief           Print statistics
 * \param[in]       handle: Protocol instance handle
 */
static void print_statistics(xgl_handle_t handle)
{
    xgl_statistics_t stats;
    xgl_error_t err = xgl_stats_get(handle, &stats);
    
    if (err != XGL_OK) {
        printf("[STATS] Failed to get statistics: %s\n", xgl_error_string(err));
        return;
    }
    
    printf("\n[STATS] Protocol Statistics:\n");
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
    printf("    CRC8 Errors:   %llu\n", (unsigned long long)stats.rx_crc8_errors);
    printf("    CRC16 Errors:  %llu\n", (unsigned long long)stats.rx_crc16_errors);
    printf("  Avg RTT:       %u ms\n", stats.avg_rtt_ms);
    printf("  Memory Used:   %zu bytes\n", stats.memory_used);
    printf("  Memory Peak:   %zu bytes\n", stats.memory_peak);
}

/*---------------------------------------------------------------------------*/
/* Main Function                                                             */
/*---------------------------------------------------------------------------*/

int main(void)
{
    printf("=================================================\n");
    printf("  x_gen_link Echo Server Example\n");
    printf("=================================================\n");
    printf("This example demonstrates basic send/receive\n");
    printf("functionality by echoing received data back.\n");
    printf("=================================================\n\n");
    
    /* Setup physical layer operations */
    xgl_phy_ops_t phy = {
        .tx = phy_tx,
        .rx = phy_rx,
        .user_data = NULL
    };
    
    /* Setup route table (routes to node 1 and node 2) */
    xgl_route_item_t routes[] = {
        {
            .target_id = 1,  /* Route to self (for loopback testing) */
            .phy = &phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        },
        {
            .target_id = 2,  /* Route to node 2 */
            .phy = &phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        }
    };
    
    /* Get default configuration */
    xgl_config_t config;
    xgl_config_get_default(&config);
    
    /* Customize configuration */
    config.name = "echo_server";
    config.source_id = 1;  /* This node is ID 1 */
    config.route_table = routes;
    config.route_table_len = 2;  /* Two routes now */
    config.rx_callback = on_receive;
    config.error_callback = on_error;
    config.callback_user_data = NULL;  /* Will be set to handle after creation */
    
    printf("[INIT] Creating protocol instance...\n");
    xgl_handle_t handle = xgl_create(&config);
    if (handle == NULL) {
        printf("[ERROR] Failed to create protocol instance\n");
        return -1;
    }
    printf("[INIT] Protocol instance created successfully\n");
    
    printf("[INIT] Initializing protocol...\n");
    xgl_error_t err = xgl_init(handle);
    if (err != XGL_OK) {
        printf("[ERROR] Failed to initialize protocol: %s\n", xgl_error_string(err));
        xgl_destroy(handle);
        return -1;
    }
    printf("[INIT] Protocol initialized successfully\n");
    
    /* Print initial statistics */
    print_statistics(handle);
    
    /* Test echo operations with real protocol stack */
    printf("\n=================================================\n");
    printf("  Testing Echo Operations (Full Protocol Stack)\n");
    printf("=================================================\n");
    printf("Note: Using loopback - sending to self (node 1)\n\n");
    
    /* Send test messages - they will be transmitted via PHY TX,
     * looped back via PHY RX, and processed through all layers */
    send_test_message(handle, 1, "Hello, Echo Server!");  /* Send to self */
    
    /* Run protocol to process TX and RX */
    printf("\n[MAIN] Running protocol processing (iteration 1)...\n");
    for (int i = 0; i < 10; i++) {
        xgl_run(handle, 100);  /* Call at 100 Hz */
    }
    
    send_test_message(handle, 1, "Testing 1-2-3");  /* Send to self */
    
    printf("\n[MAIN] Running protocol processing (iteration 2)...\n");
    for (int i = 0; i < 10; i++) {
        xgl_run(handle, 100);
    }
    
    send_test_message(handle, 1, "x_gen_link protocol");  /* Send to self */
    
    printf("\n[MAIN] Running protocol processing (iteration 3)...\n");
    for (int i = 0; i < 10; i++) {
        xgl_run(handle, 100);
    }
    
    /* Print final statistics */
    print_statistics(handle);
    
    /* Cleanup */
    printf("\n[CLEANUP] Destroying protocol instance...\n");
    xgl_destroy(handle);
    printf("[CLEANUP] Done\n");
    
    printf("\n=================================================\n");
    printf("  Echo Server Example Complete\n");
    printf("=================================================\n");
    
    return 0;
}
