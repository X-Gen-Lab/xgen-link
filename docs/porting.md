# Porting Guide

Porting xgen-link means providing three things: a PHY driver, platform services, and a bounded memory plan.

## PHY Contract

Implement `xgl_phy_ops_t`:

```c
xgl_error_t tx(const uint8_t* data, size_t len, void* user_data);
xgl_error_t rx(uint8_t* buffer, size_t* len, void* user_data);
```

`tx` should transmit the complete frame buffer. `rx` should copy at most `*len` bytes into `buffer`, update `*len` with bytes read, and return `XGL_OK` with `*len == 0` when no data is available.

## Scheduler Contract

Call `xgl_run(handle, freq_hz)` periodically. Bare-metal ports usually call it from the main loop. RTOS ports usually call it from a task or timer callback. Do not call blocking drivers from interrupt context.

## Memory Contract

Use presets as starting points, then measure:

- TX pool size
- RX cache size
- route count
- fragmentation buffers
- maximum stack depth on target
- allocator call count during init and traffic bursts

Example ports are in `examples/platforms`:

- `bare_metal_port.c`
- `freertos_port.c`
- `windows_mock_port.c`
