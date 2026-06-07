# Echo Server Example

## Overview

This example runs two XGL instances in one process:

- node `1` is the echo server.
- node `2` is the client.
- two simulated point-to-point PHY channels connect node `1 -> 2` and `2 -> 1`.

The client sends application messages to node `1`. The server receives each
message through `xgl_rx_callback_t` and echoes the same payload back to the
source node. This keeps the example aligned with the production network model:
normal unicast traffic uses distinct 16-bit source and target node IDs.

## What It Demonstrates

- Creating and initializing multiple protocol instances.
- Configuring per-node route tables and PHY callbacks.
- Delivering application data through a `uint16_t source_id` receive callback.
- Sending responses with `xgl_send()`.
- Polling both nodes with `xgl_run()`.
- Reading datalink, CRC16, retry, RTT, and memory statistics.
- Verifying the example through an acceptance check.

## Build and Run

From the project root:

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target echo_server
.\build\gcc-test\examples\echo_server\echo_server.exe
```

On non-Windows generators, the executable path may be:

```sh
./build/gcc-test/examples/echo_server/echo_server
```

The program exits with status `0` only when the server receives and echoes all
  test messages and the client receives the echoed responses.

## Code Structure

`sim_channel_t`
: A small FIFO-style byte buffer used by the simulated PHY.

`sim_phy_ctx_t`
: Binds one TX channel and one RX channel to a node. Node `1` transmits on the
  `1 -> 2` channel and receives on the `2 -> 1` channel; node `2` uses the
  reverse binding.

`on_receive()`
: Shared application callback. The server context has `echo_enabled=true`, so it
  echoes payloads back to the source. The client context only records received
  responses.

`pump_protocol()`
: Calls `xgl_run()` on both nodes so TX, RX, parser, network, and transport work
  can progress without threads.

## Key API Pattern

```c
static void on_receive(xgl_handle_t handle,
                       uint16_t source_id,
                       uint8_t data_type,
                       const uint8_t* data,
                       size_t len,
                       void* user_data)
{
    xgl_tx_data_t response = {
        .target_id = source_id,
        .data_type = data_type,
        .data = data,
        .data_len = len,
        .reliable = false,
        .priority = 0
    };

    (void)xgl_send(handle, &response);
}
```

For production code, keep the payload buffer valid until `xgl_send()` returns.
Use `.reliable = true` when the application requires retransmission and ordered
delivery; this compact example keeps reliability-specific behavior out of the
basic PHY and callback walkthrough.

## Adapting to Hardware

Replace the simulated PHY callbacks with the board driver contract:

```c
static xgl_error_t uart_tx(const uint8_t* data, size_t len, void* user_data)
{
    uart_handle_t* uart = (uart_handle_t*)user_data;
    return uart_write_all(uart, data, len) ? XGL_OK : XGL_ERR_TX_FAILED;
}

static xgl_error_t uart_rx(uint8_t* buffer, size_t* len, void* user_data)
{
    uart_handle_t* uart = (uart_handle_t*)user_data;
    *len = uart_read_available(uart, buffer, *len);
    return XGL_OK;
}
```

In an MCU port, ISR code should only move bytes into a driver or ring buffer.
Call `xgl_run()` from a task or main loop, not from the ISR, because parsing,
authentication, retransmission, and callbacks are protocol work.

## Troubleshooting

- If no payload reaches the callback, check route target IDs, local
  `source_id`, and whether `xgl_run()` is called often enough.
- If TX succeeds but RX stays empty, inspect the PHY callback bindings. Each node
  must transmit into the peer's receive channel.
- If reliable traffic stalls, inspect retry and ACK statistics and ensure the
  peer node is also being polled.
- If CRC16 errors appear, the simulated or hardware PHY is corrupting bytes or
  delivering partial frames incorrectly.

## Related Documentation

- `docs/zh/getting-started/examples.md`
- `docs/zh/guide/send-api.md`
- `docs/zh/protocol/reliability.md`
- `docs/zh/guide/porting.md`
