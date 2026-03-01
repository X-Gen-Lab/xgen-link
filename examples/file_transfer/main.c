/**
 * \file            main.c
 * \brief           File transfer example - demonstrates reliable transfer with fragmentation
 * \author          Nexus Team
 * \date            2026-02-28
 *
 * \details         This example demonstrates:
 *                  - Reliable file transfer using ACK/NACK
 *                  - Automatic fragmentation for large files
 *                  - Progress reporting during transfer
 *                  - Error handling and retry logic
 *                  - Statistics monitoring
 *                  - Simulated file data transfer
 */

#include <xgl/xgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*---------------------------------------------------------------------------*/
/* Configuration                                                             */
/*---------------------------------------------------------------------------*/

#define FILE_CHUNK_SIZE         512     /**< Size of each file chunk */
#define SIMULATED_FILE_SIZE     4096    /**< Size of simulated file (4KB) */
#define MAX_RETRIES             5       /**< Maximum retry attempts */

/*---------------------------------------------------------------------------*/
/* File Transfer State                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           File transfer state
 */
typedef enum {
    TRANSFER_IDLE,              /**< No transfer in progress */
    TRANSFER_IN_PROGRESS,       /**< Transfer in progress */
    TRANSFER_COMPLETE,          /**< Transfer completed successfully */
    TRANSFER_FAILED             /**< Transfer failed */
} transfer_state_t;

/**
 * \brief           File transfer context
 */
typedef struct {
    uint8_t* file_data;         /**< File data buffer */
    size_t file_size;           /**< Total file size */
    size_t bytes_sent;          /**< Bytes sent so far */
    size_t bytes_received;      /**< Bytes received so far */
    transfer_state_t state;     /**< Transfer state */
    uint32_t start_time;        /**< Transfer start time */
    uint32_t end_time;          /**< Transfer end time */
    uint8_t* rx_buffer;         /**< Receive buffer */
    size_t rx_buffer_size;      /**< Receive buffer size */
} file_transfer_ctx_t;

static file_transfer_ctx_t sender_ctx;
static file_transfer_ctx_t receiver_ctx;

/*---------------------------------------------------------------------------*/
/* Simulated Physical Layer                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Simulated communication channel
 */
typedef struct {
    uint8_t* buffer;
    size_t buffer_size;
    size_t data_len;
    size_t read_pos;
} sim_channel_t;

static sim_channel_t sender_to_receiver;
static sim_channel_t receiver_to_sender;

/**
 * \brief           Initialize simulated channel
 */
static void sim_channel_init(sim_channel_t* channel, size_t buffer_size)
{
    channel->buffer = (uint8_t*)malloc(buffer_size);
    channel->buffer_size = buffer_size;
    channel->data_len = 0;
    channel->read_pos = 0;
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
 * \brief           Write to simulated channel (accumulates data)
 */
static xgl_error_t sim_channel_write(sim_channel_t* channel, 
                                     const uint8_t* data, 
                                     size_t len)
{
    /* Check if there's enough space */
    if (channel->data_len + len > channel->buffer_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Append data to buffer */
    memcpy(channel->buffer + channel->data_len, data, len);
    channel->data_len += len;
    
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
    
    /* Reset buffer when all data has been read */
    if (channel->read_pos >= channel->data_len) {
        channel->data_len = 0;
        channel->read_pos = 0;
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Physical Layer Callbacks - Sender                                         */
/*---------------------------------------------------------------------------*/

static xgl_error_t sender_phy_tx(const uint8_t* data, size_t len, void* user_data)
{
    (void)user_data;
    return sim_channel_write(&sender_to_receiver, data, len);
}

static xgl_error_t sender_phy_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    (void)user_data;
    return sim_channel_read(&receiver_to_sender, buffer, len);
}

/*---------------------------------------------------------------------------*/
/* Physical Layer Callbacks - Receiver                                       */
/*---------------------------------------------------------------------------*/

static xgl_error_t receiver_phy_tx(const uint8_t* data, size_t len, void* user_data)
{
    (void)user_data;
    return sim_channel_write(&receiver_to_sender, data, len);
}

static xgl_error_t receiver_phy_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    (void)user_data;
    return sim_channel_read(&sender_to_receiver, buffer, len);
}

/*---------------------------------------------------------------------------*/
/* Protocol Callbacks - Sender                                               */
/*---------------------------------------------------------------------------*/

static void sender_on_receive(xgl_handle_t handle,
                              uint8_t source_id,
                              uint8_t data_type,
                              const uint8_t* data,
                              size_t len,
                              void* user_data)
{
    (void)handle;
    (void)source_id;
    (void)data_type;
    (void)data;
    (void)len;
    (void)user_data;
    
    /* Sender typically receives ACKs, which are handled internally */
}

static void sender_on_error(xgl_handle_t handle,
                           xgl_error_t error,
                           const char* message,
                           void* user_data)
{
    (void)handle;
    (void)user_data;
    
    printf("[SENDER ERROR] Code %d: %s\n", error, message);
    sender_ctx.state = TRANSFER_FAILED;
}

/*---------------------------------------------------------------------------*/
/* Protocol Callbacks - Receiver                                             */
/*---------------------------------------------------------------------------*/

static void receiver_on_receive(xgl_handle_t handle,
                                uint8_t source_id,
                                uint8_t data_type,
                                const uint8_t* data,
                                size_t len,
                                void* user_data)
{
    (void)handle;
    (void)source_id;
    (void)user_data;
    
    if (data_type == 0x01) {  /* File data chunk */
        /* Append received data to buffer */
        if (receiver_ctx.bytes_received + len <= receiver_ctx.rx_buffer_size) {
            memcpy(receiver_ctx.rx_buffer + receiver_ctx.bytes_received, data, len);
            receiver_ctx.bytes_received += len;
            
            /* Calculate and display progress */
            float progress = (float)receiver_ctx.bytes_received / SIMULATED_FILE_SIZE * 100.0f;
            printf("[RECEIVER] Received chunk: %zu bytes (Total: %zu/%d bytes, %.1f%%)\n",
                   len, receiver_ctx.bytes_received, SIMULATED_FILE_SIZE, progress);
            
            /* Check if transfer is complete */
            if (receiver_ctx.bytes_received >= SIMULATED_FILE_SIZE) {
                receiver_ctx.state = TRANSFER_COMPLETE;
                receiver_ctx.end_time = (uint32_t)time(NULL);
                printf("[RECEIVER] File transfer complete!\n");
            }
        } else {
            printf("[RECEIVER ERROR] Buffer overflow\n");
            receiver_ctx.state = TRANSFER_FAILED;
        }
    }
}

static void receiver_on_error(xgl_handle_t handle,
                             xgl_error_t error,
                             const char* message,
                             void* user_data)
{
    (void)handle;
    (void)user_data;
    
    printf("[RECEIVER ERROR] Code %d: %s\n", error, message);
    receiver_ctx.state = TRANSFER_FAILED;
}

/*---------------------------------------------------------------------------*/
/* File Transfer Functions                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Generate simulated file data
 */
static void generate_file_data(uint8_t* buffer, size_t size)
{
    printf("[SENDER] Generating %zu bytes of file data...\n", size);
    
    /* Generate pseudo-random data with pattern */
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)((i * 7 + 13) % 256);
    }
    
    printf("[SENDER] File data generated\n");
}

/**
 * \brief           Send file chunk
 */
static xgl_error_t send_file_chunk(xgl_handle_t handle, 
                                   uint8_t target_id,
                                   const uint8_t* data,
                                   size_t len)
{
    xgl_tx_data_t tx_data = {
        .target_id = target_id,
        .data_type = 0x01,  /* File data type */
        .data = data,
        .data_len = len,
        .reliable = true,   /* Reliable transmission with ACK */
        .priority = 5       /* High priority */
    };
    
    return xgl_send(handle, &tx_data);
}

/**
 * \brief           Transfer file
 */
static void transfer_file(xgl_handle_t sender, xgl_handle_t receiver, uint8_t target_id)
{
    printf("\n=================================================\n");
    printf("  Starting File Transfer\n");
    printf("=================================================\n");
    printf("File size: %d bytes\n", SIMULATED_FILE_SIZE);
    printf("Chunk size: %d bytes\n", FILE_CHUNK_SIZE);
    printf("Expected chunks: %d\n", (SIMULATED_FILE_SIZE + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
    printf("=================================================\n\n");
    
    sender_ctx.state = TRANSFER_IN_PROGRESS;
    sender_ctx.bytes_sent = 0;
    sender_ctx.start_time = (uint32_t)time(NULL);
    
    size_t offset = 0;
    int chunk_num = 0;
    
    while (offset < sender_ctx.file_size && sender_ctx.state == TRANSFER_IN_PROGRESS) {
        size_t chunk_size = sender_ctx.file_size - offset;
        if (chunk_size > FILE_CHUNK_SIZE) {
            chunk_size = FILE_CHUNK_SIZE;
        }
        
        chunk_num++;
        printf("[SENDER] Sending chunk %d: %zu bytes (offset %zu)\n", 
               chunk_num, chunk_size, offset);
        
        xgl_error_t err = send_file_chunk(sender, target_id, 
                                         sender_ctx.file_data + offset, 
                                         chunk_size);
        
        if (err != XGL_OK) {
            printf("[SENDER ERROR] Failed to send chunk: %s\n", xgl_error_string(err));
            sender_ctx.state = TRANSFER_FAILED;
            break;
        }
        
        sender_ctx.bytes_sent += chunk_size;
        offset += chunk_size;
        
        /* Display progress */
        float progress = (float)sender_ctx.bytes_sent / sender_ctx.file_size * 100.0f;
        printf("[SENDER] Progress: %zu/%zu bytes (%.1f%%)\n\n",
               sender_ctx.bytes_sent, sender_ctx.file_size, progress);
        
        /* Process protocol to allow ACKs to be received */
        for (int i = 0; i < 10; i++) {
            xgl_run(sender, 100);
            xgl_run(receiver, 100);
        }
    }
    
    if (sender_ctx.state == TRANSFER_IN_PROGRESS) {
        sender_ctx.state = TRANSFER_COMPLETE;
        sender_ctx.end_time = (uint32_t)time(NULL);
        printf("[SENDER] All chunks sent successfully\n");
    }
}

/**
 * \brief           Verify received file
 */
static bool verify_file(const uint8_t* original, const uint8_t* received, size_t size)
{
    printf("\n[VERIFY] Verifying received file...\n");
    
    for (size_t i = 0; i < size; i++) {
        if (original[i] != received[i]) {
            printf("[VERIFY ERROR] Mismatch at byte %zu: expected 0x%02X, got 0x%02X\n",
                   i, original[i], received[i]);
            return false;
        }
    }
    
    printf("[VERIFY] File verification successful! All %zu bytes match.\n", size);
    return true;
}

/**
 * \brief           Print transfer statistics
 */
static void print_transfer_stats(void)
{
    printf("\n=================================================\n");
    printf("  File Transfer Statistics\n");
    printf("=================================================\n");
    
    if (sender_ctx.state == TRANSFER_COMPLETE && receiver_ctx.state == TRANSFER_COMPLETE) {
        uint32_t duration = sender_ctx.end_time - sender_ctx.start_time;
        if (duration == 0) duration = 1;  /* Avoid division by zero */
        
        float throughput = (float)sender_ctx.file_size / duration;
        
        printf("Status: SUCCESS\n");
        printf("File size: %zu bytes\n", sender_ctx.file_size);
        printf("Bytes sent: %zu\n", sender_ctx.bytes_sent);
        printf("Bytes received: %zu\n", receiver_ctx.bytes_received);
        printf("Duration: %u seconds\n", duration);
        printf("Throughput: %.2f bytes/sec\n", throughput);
    } else {
        printf("Status: FAILED\n");
        printf("Sender state: %d\n", sender_ctx.state);
        printf("Receiver state: %d\n", receiver_ctx.state);
        printf("Bytes sent: %zu\n", sender_ctx.bytes_sent);
        printf("Bytes received: %zu\n", receiver_ctx.bytes_received);
    }
    
    printf("=================================================\n");
}

/**
 * \brief           Print protocol statistics
 */
static void print_protocol_stats(xgl_handle_t sender, xgl_handle_t receiver)
{
    xgl_statistics_t sender_stats, receiver_stats;
    
    printf("\n=================================================\n");
    printf("  Protocol Statistics\n");
    printf("=================================================\n");
    
    if (xgl_stats_get(sender, &sender_stats) == XGL_OK) {
        printf("\nSender:\n");
        printf("  TX Packets:    %llu\n", (unsigned long long)sender_stats.transport.tx_packets);
        printf("  TX Bytes:      %llu\n", (unsigned long long)sender_stats.transport.tx_bytes);
        printf("  TX Errors:     %llu\n", (unsigned long long)sender_stats.transport.tx_errors);
        printf("  TX Retries:    %llu\n", (unsigned long long)sender_stats.tx_retries);
        printf("  RX Packets:    %llu\n", (unsigned long long)sender_stats.transport.rx_packets);
        printf("  Avg RTT:       %u ms\n", sender_stats.avg_rtt_ms);
        printf("  Memory Used:   %zu bytes\n", sender_stats.memory_used);
    }
    
    if (xgl_stats_get(receiver, &receiver_stats) == XGL_OK) {
        printf("\nReceiver:\n");
        printf("  TX Packets:    %llu\n", (unsigned long long)receiver_stats.transport.tx_packets);
        printf("  TX Bytes:      %llu\n", (unsigned long long)receiver_stats.transport.tx_bytes);
        printf("  RX Packets:    %llu\n", (unsigned long long)receiver_stats.transport.rx_packets);
        printf("  RX Bytes:      %llu\n", (unsigned long long)receiver_stats.transport.rx_bytes);
        printf("  RX Errors:     %llu\n", (unsigned long long)receiver_stats.transport.rx_errors);
        printf("  CRC Errors:    %llu\n", (unsigned long long)(receiver_stats.rx_crc8_errors + receiver_stats.rx_crc16_errors));
        printf("  Memory Used:   %zu bytes\n", receiver_stats.memory_used);
    }
    
    printf("=================================================\n");
}

/*---------------------------------------------------------------------------*/
/* Main Function                                                             */
/*---------------------------------------------------------------------------*/

int main(void)
{
    printf("=================================================\n");
    printf("  x_gen_link File Transfer Example\n");
    printf("=================================================\n");
    printf("This example demonstrates reliable file transfer\n");
    printf("with automatic fragmentation and progress reporting.\n");
    printf("=================================================\n\n");
    
    /* Initialize simulated channels */
    sim_channel_init(&sender_to_receiver, 2048);
    sim_channel_init(&receiver_to_sender, 2048);
    
    /* Initialize file transfer contexts */
    sender_ctx.file_data = (uint8_t*)malloc(SIMULATED_FILE_SIZE);
    sender_ctx.file_size = SIMULATED_FILE_SIZE;
    sender_ctx.bytes_sent = 0;
    sender_ctx.state = TRANSFER_IDLE;
    
    receiver_ctx.rx_buffer = (uint8_t*)malloc(SIMULATED_FILE_SIZE);
    receiver_ctx.rx_buffer_size = SIMULATED_FILE_SIZE;
    receiver_ctx.bytes_received = 0;
    receiver_ctx.state = TRANSFER_IDLE;
    
    /* Generate file data */
    generate_file_data(sender_ctx.file_data, sender_ctx.file_size);
    
    /* Setup physical layer operations */
    static xgl_phy_ops_t sender_phy;
    sender_phy.tx = sender_phy_tx;
    sender_phy.rx = sender_phy_rx;
    sender_phy.user_data = NULL;
    
    static xgl_phy_ops_t receiver_phy;
    receiver_phy.tx = receiver_phy_tx;
    receiver_phy.rx = receiver_phy_rx;
    receiver_phy.user_data = NULL;
    
    /* Setup route tables */
    static xgl_route_item_t sender_routes[1];
    sender_routes[0].target_id = 2;  /* Receiver */
    sender_routes[0].phy = &sender_phy;
    sender_routes[0].max_frame_size = 256;
    sender_routes[0].read_freq_hz = 100;
    sender_routes[0].metric = 0;
    
    static xgl_route_item_t receiver_routes[1];
    receiver_routes[0].target_id = 1;  /* Sender */
    receiver_routes[0].phy = &receiver_phy;
    receiver_routes[0].max_frame_size = 256;
    receiver_routes[0].read_freq_hz = 100;
    receiver_routes[0].metric = 0;
    
    /* Configure sender */
    xgl_config_t sender_config;
    xgl_config_get_default(&sender_config);
    sender_config.name = "file_sender";
    sender_config.source_id = 1;
    sender_config.route_table = sender_routes;
    sender_config.route_table_len = 1;
    sender_config.rx_callback = sender_on_receive;
    sender_config.error_callback = sender_on_error;
    sender_config.protocol.max_retry_count = MAX_RETRIES;
    sender_config.protocol.window_size = 32;  /* Increase window size for file transfer */
    sender_config.features.enable_fragmentation = true;
    
    /* Configure receiver */
    xgl_config_t receiver_config;
    xgl_config_get_default(&receiver_config);
    receiver_config.name = "file_receiver";
    receiver_config.source_id = 2;
    receiver_config.route_table = receiver_routes;
    receiver_config.route_table_len = 1;
    receiver_config.rx_callback = receiver_on_receive;
    receiver_config.error_callback = receiver_on_error;
    receiver_config.features.enable_fragmentation = true;
    
    /* Create protocol instances */
    printf("[INIT] Creating sender instance...\n");
    xgl_handle_t sender = xgl_create(&sender_config);
    if (sender == NULL) {
        printf("[ERROR] Failed to create sender instance\n");
        goto cleanup;
    }
    
    printf("[INIT] Creating receiver instance...\n");
    xgl_handle_t receiver = xgl_create(&receiver_config);
    if (receiver == NULL) {
        printf("[ERROR] Failed to create receiver instance\n");
        xgl_destroy(sender);
        goto cleanup;
    }
    
    /* Initialize instances */
    printf("[INIT] Initializing sender...\n");
    if (xgl_init(sender) != XGL_OK) {
        printf("[ERROR] Failed to initialize sender\n");
        xgl_destroy(sender);
        xgl_destroy(receiver);
        goto cleanup;
    }
    
    printf("[INIT] Initializing receiver...\n");
    if (xgl_init(receiver) != XGL_OK) {
        printf("[ERROR] Failed to initialize receiver\n");
        xgl_destroy(sender);
        xgl_destroy(receiver);
        goto cleanup;
    }
    
    printf("[INIT] Initialization complete\n\n");
    
    /* Start file transfer */
    transfer_file(sender, receiver, 2);
    
    /* Run protocol processing to complete transfer */
    printf("\n[MAIN] Processing protocol messages...\n");
    int iterations = 0;
    const int max_iterations = 1000;
    
    while ((sender_ctx.state == TRANSFER_IN_PROGRESS || 
            receiver_ctx.state != TRANSFER_COMPLETE) &&
           iterations < max_iterations) {
        
        xgl_run(sender, 100);
        xgl_run(receiver, 100);
        iterations++;
        
        /* Small delay to simulate real-time processing */
        if (iterations % 10 == 0) {
            printf(".");
            fflush(stdout);
        }
    }
    printf("\n");
    
    if (iterations >= max_iterations) {
        printf("[WARNING] Maximum iterations reached\n");
    }
    
    /* Verify file integrity */
    if (receiver_ctx.state == TRANSFER_COMPLETE) {
        verify_file(sender_ctx.file_data, receiver_ctx.rx_buffer, SIMULATED_FILE_SIZE);
    }
    
    /* Print statistics */
    print_transfer_stats();
    print_protocol_stats(sender, receiver);
    
    /* Cleanup */
    printf("\n[CLEANUP] Destroying protocol instances...\n");
    xgl_destroy(sender);
    xgl_destroy(receiver);
    
cleanup:
    /* Free resources */
    if (sender_ctx.file_data) {
        free(sender_ctx.file_data);
    }
    if (receiver_ctx.rx_buffer) {
        free(receiver_ctx.rx_buffer);
    }
    sim_channel_free(&sender_to_receiver);
    sim_channel_free(&receiver_to_sender);
    
    printf("[CLEANUP] Done\n");
    
    printf("\n=================================================\n");
    printf("  File Transfer Example Complete\n");
    printf("=================================================\n");
    
    return (sender_ctx.state == TRANSFER_COMPLETE && 
            receiver_ctx.state == TRANSFER_COMPLETE) ? 0 : -1;
}
