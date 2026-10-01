# Porting

The protocol is C11 and consumes five independent foundation packages plus caller-supplied I/O and time. It contains no OS, hardware clock, mutex, or scheduler backend. See [modular migration](modular-migration.md) for dependency entry points.

## PHY Contract

```c
xgl_error_t tx(const uint8_t *data, size_t len, void *user_data);
xgl_error_t rx(uint8_t *buffer, size_t *len, void *user_data);
```

TX receives one complete frame. Return `XGL_OK` only after consuming or copying every byte. Do not retain the pointer, including for DMA. Return temporary backpressure only when the frame was not accepted; the interface cannot represent partial-frame acceptance.

RX receives the available buffer capacity in `*len` and writes the actual byte count back. It must never exceed capacity. With no data, return `XGL_OK` and zero length. Calls must be nonblocking or have a bounded execution time; callbacks cannot reenter the instance.

## Time and Scheduling

The application supplies `uint32_t now_ms` to send, step, and timeout queries. Use the same monotonic millisecond source for the lifetime of the instance. Unsigned wraparound is supported; intervals and elapsed comparisons must stay below `2^31` milliseconds.

Call `xgl_step()` regularly or on RX/timer events. `xgl_next_timeout()` provides the next relative wakeup. No global time registration or protocol timer thread is required.

## Storage and Lifetime

Prefer aligned caller storage with `xgl_init_static()`. Query the exact target requirements first; do not copy a host ABI size into firmware. Dynamic creation accepts an explicit `xgm_allocator_t` whose callbacks receive its `ctx`, return suitably aligned storage, and release the one workspace at destroy.

Workspace, immutable configuration, PHY descriptors, provider descriptors, and contexts must remain valid at fixed addresses until destruction. Separate instances need separate workspaces and independently managed contexts.

## ISR and RTOS Integration

An ISR may fill an application-owned ring or signal the protocol task. Parsing, authentication, ACK processing, and user callbacks run in the serialized task/main-loop context.

Use one owner task per instance, or an application lock around every API including stats and destroy. A recursive mutex does not make callback reentry safe. Complete asynchronous driver ownership inside the adapter before returning from PHY TX.

## Verification

Exercise split frames, bursts, full RX rings, PHY backpressure, lost ACKs, clock wrap, and resource exhaustion on the real driver. Use the Cortex-M0 footprint consumer as a link-measurement reference; its startup and empty RX callback are not a board support package.
