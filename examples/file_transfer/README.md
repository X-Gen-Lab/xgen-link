# File Transfer Example

## Overview

This example demonstrates reliable file transfer using the xgen-link protocol stack. It showcases how to transfer large files with automatic fragmentation, progress reporting, error handling, and data integrity verification.

## What This Example Demonstrates

1. **Reliable File Transfer**: Using ACK/NACK for guaranteed delivery
2. **Automatic Fragmentation**: Breaking large files into manageable chunks
3. **Progress Reporting**: Real-time progress updates during transfer
4. **Error Handling**: Comprehensive error detection and recovery
5. **Data Integrity**: Verification of received data against original
6. **Statistics Monitoring**: Detailed transfer and protocol statistics
7. **Two-Node Communication**: Sender and receiver instances

## Key Features

### Reliable Transmission
- Every chunk is sent with `reliable = true`
- Automatic ACK/NACK handling by protocol stack
- Configurable retry count (default: 5 retries)
- Exponential backoff on retransmission

### Fragmentation Support
- Large files automatically split into chunks
- Configurable chunk size (default: 512 bytes)
- Transparent reassembly at receiver
- Handles out-of-order delivery

### Progress Reporting
- Real-time progress updates for sender
- Real-time progress updates for receiver
- Percentage completion display
- Throughput calculation

### Error Handling
- Buffer overflow detection
- Transmission failure handling
- Timeout management
- Error callbacks for debugging

## Building

From the project root directory:

```bash
# Configure the build
cmake -B build -S .

# Build the file transfer example
cmake --build build --target file_transfer

# Run the example
./build/examples/file_transfer/file_transfer
```

On Windows:

```cmd
cmake -B build -S .
cmake --build build --target file_transfer --config Release
.\build\examples\file_transfer\Release\file_transfer.exe
```

## Code Structure

### Main Components

1. **File Transfer Context** (`file_transfer_ctx_t`)
   - Tracks transfer state (idle, in progress, complete, failed)
   - Maintains byte counters for progress
   - Stores timing information for throughput calculation

2. **Simulated Physical Layer**
   - Two-way communication channels
   - Simulates UART/SPI/I2C behavior
   - In real applications, replace with actual hardware drivers

3. **Sender Logic**
   - Generates simulated file data
   - Splits file into chunks
   - Sends chunks with progress reporting
   - Monitors transfer completion

4. **Receiver Logic**
   - Receives chunks via callback
   - Reassembles file in buffer
   - Reports progress
   - Verifies data integrity

5. **Statistics and Verification**
   - Transfer statistics (bytes, duration, throughput)
   - Protocol statistics (packets, errors, RTT)
   - Data integrity verification

### Key API Functions Used

```c
/* Configuration */
xgl_config_get_default(&config);
config.enable_fragmentation = true;  // Enable fragmentation support

/* Reliable transmission */
xgl_tx_data_t tx_data = {
    .target_id = 2,
    .data_type = 0x01,
    .data = chunk_data,
    .data_len = chunk_size,
    .reliable = true,    // Request ACK
    .priority = 5        // High priority
};
xgl_send(handle, &tx_data);

/* Runtime processing */
xgl_run(sender, 100);    // Process sender at 100 Hz
xgl_run(receiver, 100);  // Process receiver at 100 Hz

/* Statistics */
xgl_stats_get(handle, &stats);
```

## Expected Output

```
=================================================
  xgen-link File Transfer Example
=================================================
This example demonstrates reliable file transfer
with automatic fragmentation and progress reporting.
=================================================

[SENDER] Generating 4096 bytes of file data...
[SENDER] File data generated
[INIT] Creating sender instance...
[INIT] Creating receiver instance...
[INIT] Initializing sender...
[INIT] Initializing receiver...
[INIT] Initialization complete

=================================================
  Starting File Transfer
=================================================
File size: 4096 bytes
Chunk size: 512 bytes
Expected chunks: 8
=================================================

[SENDER] Sending chunk 1: 512 bytes (offset 0)
[SENDER] Progress: 512/4096 bytes (12.5%)

[RECEIVER] Received chunk: 512 bytes (Total: 512/4096 bytes, 12.5%)

[SENDER] Sending chunk 2: 512 bytes (offset 512)
[SENDER] Progress: 1024/4096 bytes (25.0%)

[RECEIVER] Received chunk: 512 bytes (Total: 1024/4096 bytes, 25.0%)

...

[SENDER] All chunks sent successfully
[RECEIVER] File transfer complete!

[VERIFY] Verifying received file...
[VERIFY] File verification successful! All 4096 bytes match.

=================================================
  File Transfer Statistics
=================================================
Status: SUCCESS
File size: 4096 bytes
Bytes sent: 4096
Bytes received: 4096
Duration: 1 seconds
Throughput: 4096.00 bytes/sec
=================================================

=================================================
  Protocol Statistics
=================================================

Sender:
  TX Packets:    8
  TX Bytes:      4096
  TX Errors:     0
  TX Retries:    0
  RX Packets:    8
  Avg RTT:       10 ms
  Memory Used:   8192 bytes

Receiver:
  TX Packets:    8
  TX Bytes:      256
  RX Packets:    8
  RX Bytes:      4096
  RX Errors:     0
  CRC Errors:    0
  Memory Used:   8192 bytes
=================================================

[CLEANUP] Destroying protocol instances...
[CLEANUP] Done

=================================================
  File Transfer Example Complete
=================================================
```

## Configuration Options

### File Transfer Parameters

```c
#define FILE_CHUNK_SIZE         512     /* Size of each chunk */
#define SIMULATED_FILE_SIZE     4096    /* Total file size */
#define MAX_RETRIES             5       /* Maximum retry attempts */
```

### Protocol Configuration

```c
xgl_config_t config;
xgl_config_get_default(&config);

/* Customize for file transfer */
config.max_retry_count = 5;           // Retry failed chunks
config.ack_timeout_ms = 1000;         // ACK timeout
config.window_size = 8;               // Sliding window size
config.max_frame_size = 256;          // Maximum frame size
config.enable_fragmentation = true;   // Enable fragmentation
config.tx_pool_size = 8192;           // Larger TX pool for file data
config.rx_buffer_size = 1024;         // Larger RX buffer
```

## Adapting for Real Hardware

### 1. Replace Physical Layer with UART

```c
/* UART TX callback */
static xgl_error_t uart_tx(const uint8_t* data, size_t len, void* user_data)
{
    UART_Handle* uart = (UART_Handle*)user_data;

    /* Send data via UART */
    size_t sent = UART_Write(uart, data, len);
    if (sent != len) {
        return XGL_ERR_TX_FAILED;
    }

    return XGL_OK;
}

/* UART RX callback */
static xgl_error_t uart_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    UART_Handle* uart = (UART_Handle*)user_data;

    /* Read available data from UART */
    *len = UART_Read(uart, buffer, *len);

    return XGL_OK;
}

/* Setup */
UART_Handle uart = UART_Init(UART1, 115200);
xgl_phy_ops_t phy = {
    .tx = uart_tx,
    .rx = uart_rx,
    .user_data = &uart
};
```

### 2. Read File from Filesystem

```c
/* Read file from SD card or flash */
FILE* fp = fopen("firmware.bin", "rb");
if (fp == NULL) {
    printf("Failed to open file\n");
    return -1;
}

/* Get file size */
fseek(fp, 0, SEEK_END);
size_t file_size = ftell(fp);
fseek(fp, 0, SEEK_SET);

/* Allocate buffer */
uint8_t* file_data = (uint8_t*)malloc(file_size);

/* Read file */
size_t bytes_read = fread(file_data, 1, file_size, fp);
fclose(fp);

if (bytes_read != file_size) {
    printf("Failed to read file\n");
    free(file_data);
    return -1;
}

/* Transfer file */
sender_ctx.file_data = file_data;
sender_ctx.file_size = file_size;
transfer_file(sender, target_id);
```

### 3. Write Received File to Filesystem

```c
/* In receiver callback */
static void receiver_on_receive(xgl_handle_t handle,
                                uint16_t source_id,
                                uint8_t data_type,
                                const uint8_t* data,
                                size_t len,
                                void* user_data)
{
    FILE* fp = (FILE*)user_data;

    if (data_type == 0x01) {  /* File data */
        /* Write chunk to file */
        size_t written = fwrite(data, 1, len, fp);
        if (written != len) {
            printf("Failed to write to file\n");
            return;
        }

        receiver_ctx.bytes_received += len;

        /* Display progress */
        float progress = (float)receiver_ctx.bytes_received / expected_size * 100.0f;
        printf("Progress: %.1f%%\n", progress);

        /* Check if complete */
        if (receiver_ctx.bytes_received >= expected_size) {
            fclose(fp);
            printf("File transfer complete\n");
        }
    }
}

/* Setup */
FILE* output_file = fopen("received.bin", "wb");
config.rx_callback = receiver_on_receive;
config.callback_user_data = output_file;
```

### 4. Implement Main Loop

```c
int main(void)
{
    /* ... initialization ... */

    /* Open file to transfer */
    FILE* fp = fopen("firmware.bin", "rb");
    /* ... read file ... */

    /* Start transfer */
    transfer_file(sender, target_id);

    /* Main loop */
    while (sender_ctx.state == TRANSFER_IN_PROGRESS ||
           receiver_ctx.state != TRANSFER_COMPLETE) {

        /* Process protocol at 100 Hz */
        xgl_run(sender, 100);
        xgl_run(receiver, 100);

        /* Delay 10ms (100 Hz) */
        delay_ms(10);

        /* Optional: Update display, check for user abort, etc. */
    }

    /* Verify transfer */
    if (receiver_ctx.state == TRANSFER_COMPLETE) {
        printf("Transfer successful\n");
    } else {
        printf("Transfer failed\n");
    }

    /* Cleanup */
    xgl_destroy(sender);
    xgl_destroy(receiver);

    return 0;
}
```

## Use Cases

### 1. Firmware Update Over UART

```c
/* Sender (PC/Host) */
- Read firmware binary from file
- Transfer to embedded device
- Verify checksum

/* Receiver (Embedded Device) */
- Receive firmware chunks
- Write to flash memory
- Verify and reboot
```

### 2. Data Logging

```c
/* Sender (Sensor Node) */
- Collect sensor data
- Transfer log files to gateway
- Clear local storage

/* Receiver (Gateway) */
- Receive log files
- Store to SD card or cloud
- Acknowledge receipt
```

### 3. Configuration File Transfer

```c
/* Sender (Configuration Tool) */
- Load configuration file
- Transfer to device
- Wait for confirmation

/* Receiver (Device) */
- Receive configuration
- Parse and apply settings
- Send acknowledgment
```

### 4. Image Transfer for Display

```c
/* Sender (Host) */
- Load image file (BMP, PNG)
- Transfer to display controller
- Monitor progress

/* Receiver (Display Controller) */
- Receive image data
- Write to frame buffer
- Display image
```

## Performance Optimization

### Increase Chunk Size

```c
#define FILE_CHUNK_SIZE  1024  /* Larger chunks = fewer packets */
```

### Adjust Window Size

```c
config.window_size = 16;  /* More in-flight packets */
```

### Use Zero-Copy API

```c
/* Allocate buffer with header and DATA_TYPE_EXT space */
uint8_t buffer[XGL_FRAME_HEADER_SIZE + XGL_DATA_TYPE_EXT_SIZE + FILE_CHUNK_SIZE];
size_t data_offset = XGL_FRAME_HEADER_SIZE + XGL_DATA_TYPE_EXT_SIZE;

/* Read chunk after header space */
fread(buffer + data_offset, 1, FILE_CHUNK_SIZE, fp);

/* Send without copying */
xgl_tx_data_zerocopy_t tx_data = {
    .buffer = buffer,
    .buffer_size = sizeof(buffer),
    .data_offset = data_offset,
    .data_len = FILE_CHUNK_SIZE,
    .target_id = 2,
    .data_type = 0x01,
    .reliable = false,
    .priority = 5
};
xgl_send_zerocopy(handle, &tx_data);
```

Use `xgl_send()` for reliable file chunks that require ACK/retry.

### Increase Processing Frequency

```c
xgl_run(handle, 1000);  /* 1000 Hz = lower latency */
```

## Troubleshooting

### Transfer Stalls

- Check that `xgl_run()` is called regularly for both sender and receiver
- Verify physical layer callbacks are working correctly
- Check for buffer overflow in receiver
- Enable error callbacks to see error messages

### Data Corruption

- Verify CRC errors in statistics
- Check physical layer reliability
- Ensure buffers are not being reused prematurely
- Verify data integrity after transfer

### Slow Transfer

- Increase chunk size
- Increase window size
- Increase processing frequency
- Use zero-copy API
- Check for retransmissions in statistics

### Memory Issues

- Reduce chunk size
- Reduce TX/RX pool sizes
- Use streaming approach (process chunks as received)
- Check memory statistics

## Next Steps

After understanding this example, explore:

1. **Multi-Node Example** - Transfer files through intermediate nodes
2. **Zero-Copy API** - Optimize for high-throughput transfers
3. **Compression** - Enable compression for text files
4. **Encryption** - Enable encryption for secure transfers

## References

- [xgen-link User Guide](../../docs/README.md)
- [API Documentation](../../include/xgl/xgl.h)
- [Echo Server Example](../echo_server/README.md)
- [Multi-Node Example](../multi_node/README.md)

## License

Copyright (c) 2026 X-Gen Lab
