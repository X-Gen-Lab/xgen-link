# Echo Server Example

## Overview

This example demonstrates the basic usage of the x_gen_link protocol stack by implementing a simple echo server. The server receives messages and echoes them back to the sender, showcasing fundamental send/receive operations.

## What This Example Demonstrates

1. **Protocol Instance Creation**: How to create and initialize a protocol instance
2. **Configuration**: Setting up basic configuration with routes and callbacks
3. **Physical Layer Integration**: Implementing PHY callbacks (simulated for demonstration)
4. **Receive Callback**: Handling incoming data via callback function
5. **Sending Data**: Using `xgl_send()` to transmit data
6. **Error Handling**: Registering and using error callbacks
7. **Statistics**: Monitoring protocol statistics
8. **Resource Cleanup**: Properly destroying protocol instances

## Building

From the project root directory:

```bash
# Configure the build
cmake -B build -S .

# Build the echo server example
cmake --build build --target echo_server

# Run the example
./build/examples/echo_server/echo_server
```

On Windows:

```cmd
cmake -B build -S .
cmake --build build --target echo_server --config Release
.\build\examples\echo_server\Release\echo_server.exe
```

## Code Structure

### Main Components

1. **Physical Layer Simulation** (`phy_tx`, `phy_rx`)
   - Simulates UART/SPI/I2C physical layer
   - In real applications, replace with actual hardware drivers

2. **Receive Callback** (`on_receive`)
   - Called when data is received
   - Prints received data
   - Echoes data back to sender using `xgl_send()`

3. **Error Callback** (`on_error`)
   - Called when errors occur
   - Logs error information for debugging

4. **Main Loop**
   - Creates and initializes protocol instance
   - Simulates receiving messages
   - Runs protocol processing with `xgl_run()`
   - Prints statistics
   - Cleans up resources

### Key API Functions Used

```c
/* Configuration */
xgl_config_get_default(&config);  // Get default configuration

/* Instance Management */
xgl_handle_t handle = xgl_create(&config);  // Create instance
xgl_init(handle);                           // Initialize instance
xgl_destroy(handle);                        // Cleanup instance

/* Data Transmission */
xgl_send(handle, &tx_data);  // Send data (with copy)

/* Runtime Processing */
xgl_run(handle, 100);  // Process protocol (call at 100 Hz)

/* Statistics */
xgl_stats_get(handle, &stats);  // Get statistics
```

## Expected Output

```
=================================================
  x_gen_link Echo Server Example
=================================================
This example demonstrates basic send/receive
functionality by echoing received data back.
=================================================

[INIT] Creating protocol instance...
[INIT] Protocol instance created successfully
[INIT] Initializing protocol...
[INIT] Protocol initialized successfully

[STATS] Protocol Statistics:
  TX Packets:    0
  TX Bytes:      0
  ...

=================================================
  Simulating Echo Operations
=================================================

[SIM] Simulating message from node 2: "Hello, Echo Server!"

[ECHO] Received 20 bytes from node 2 (type 0x01)
[ECHO] Data: "Hello, Echo Server!"
[PHY] Transmitted 32 bytes
[ECHO] Echoed 20 bytes back to node 2

...

[STATS] Protocol Statistics:
  TX Packets:    3
  TX Bytes:      60
  ...

=================================================
  Echo Server Example Complete
=================================================
```

## Adapting for Real Hardware

To use this example with real hardware (e.g., UART):

### 1. Replace Physical Layer Callbacks

```c
/* UART TX callback */
static xgl_error_t uart_tx(const uint8_t* data, size_t len, void* user_data)
{
    UART_Handle* uart = (UART_Handle*)user_data;
    
    /* Send data via UART */
    if (UART_Write(uart, data, len) != len) {
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
```

### 2. Update Physical Layer Setup

```c
/* Initialize UART hardware */
UART_Handle uart = UART_Init(UART1, 115200);

/* Setup physical layer with UART */
xgl_phy_ops_t phy = {
    .tx = uart_tx,
    .rx = uart_rx,
    .user_data = &uart
};
```

### 3. Implement Main Loop

```c
int main(void)
{
    /* ... initialization ... */
    
    /* Main loop */
    while (1) {
        /* Process protocol at 100 Hz */
        xgl_run(handle, 100);
        
        /* Delay 10ms (100 Hz) */
        delay_ms(10);
        
        /* Optional: Check for user input, update display, etc. */
    }
    
    return 0;
}
```

## Configuration Options

The example uses default configuration. You can customize:

```c
xgl_config_t config;
xgl_config_get_default(&config);

/* Customize parameters */
config.source_id = 1;              // Local node ID
config.max_retry_count = 5;        // Retry count for reliable transmission
config.ack_timeout_ms = 1000;      // ACK timeout in milliseconds
config.window_size = 8;            // Sliding window size
config.max_frame_size = 256;       // Maximum frame size
config.tx_pool_size = 4096;        // TX memory pool size
config.rx_buffer_size = 512;       // RX buffer size
config.thread_safe = false;        // Enable for RTOS environments
```

Or use presets:

```c
/* Tiny preset (32KB RAM, 50KB Flash) */
xgl_config_get_preset_tiny(&config);

/* Small preset (64KB RAM, 100KB Flash) */
xgl_config_get_preset_small(&config);

/* Medium preset (128KB RAM, 256KB Flash) */
xgl_config_get_preset_medium(&config);

/* Large preset (256KB+ RAM, 512KB+ Flash) */
xgl_config_get_preset_large(&config);
```

## Common Use Cases

### 1. Simple Request-Response

```c
/* Client sends request */
xgl_tx_data_t request = {
    .target_id = 1,  // Echo server
    .data_type = 0x01,
    .data = (const uint8_t*)"GET_STATUS",
    .data_len = 10,
    .reliable = true,
    .priority = 0
};
xgl_send(client_handle, &request);

/* Server echoes response */
void on_receive(xgl_handle_t handle, uint8_t source_id, ...) {
    /* Echo back */
    xgl_send(handle, &response);
}
```

### 2. Sensor Data Collection

```c
/* Sensor node sends data */
uint8_t sensor_data[4] = {temp, humidity, pressure, battery};
xgl_tx_data_t tx = {
    .target_id = 1,  // Echo server (data logger)
    .data_type = 0x02,
    .data = sensor_data,
    .data_len = 4,
    .reliable = true,
    .priority = 5
};
xgl_send(sensor_handle, &tx);

/* Server logs and acknowledges */
void on_receive(...) {
    log_sensor_data(data, len);
    xgl_send(handle, &ack);  // Echo = acknowledgment
}
```

### 3. Command Echo Verification

```c
/* Send command and verify echo */
xgl_send(handle, &command);

/* Wait for echo */
void on_receive(...) {
    if (memcmp(data, sent_command, len) == 0) {
        printf("Command verified\n");
    }
}
```

## Troubleshooting

### No Data Received

- Check physical layer callbacks are implemented correctly
- Verify `xgl_run()` is called regularly (at least 10 Hz)
- Check route table configuration
- Enable error callback to see error messages

### Echo Not Working

- Verify receive callback is registered in configuration
- Check `xgl_send()` return value for errors
- Ensure target_id in route table matches sender's ID
- Check statistics for TX errors

### Memory Issues

- Reduce `tx_pool_size` and `rx_buffer_size` if RAM limited
- Use preset configurations (tiny/small/medium/large)
- Check statistics for memory usage

## Next Steps

After understanding this example, explore:

1. **File Transfer Example** - Demonstrates fragmentation for large data
2. **Multi-Node Example** - Shows routing and packet forwarding
3. **Zero-Copy API** - Learn about `xgl_send_zerocopy()` for efficiency
4. **Thread Safety** - Enable `thread_safe` for RTOS environments

## References

- [x_gen_link User Guide](../../docs/README.md)
- [API Documentation](../../include/xgl/xgl.h)
- [Architecture Overview](../../docs/README.md)
- [Porting Guide](../../docs/README.md)

## License

Copyright (c) 2026 Nexus Team
