# Porting

Porting XGL requires PHY, time, memory, and optional synchronization primitives.

## PHY Contract

```c
xgl_error_t tx(const uint8_t* data, size_t len, void* user_data);
xgl_error_t rx(uint8_t* buffer, size_t* len, void* user_data);
```

When no data is available, `rx` returns `XGL_OK` and sets `*len == 0`.

## Allocator Contract

All dynamic memory must route through the configured allocator. Strict MCU profiles disable fallback malloc.

## ISR Boundary

ISRs do not run the protocol stack. Put received data into a ring buffer, then wake the protocol task or main loop.

## Platform Examples

See `examples/platforms` for bare-metal, FreeRTOS, and Windows mock ports.
