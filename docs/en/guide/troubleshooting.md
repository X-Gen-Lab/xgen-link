# Troubleshooting Guide

This document provides diagnostic methods, debugging tools, and troubleshooting workflows for the XGL protocol stack.

## Common Issues

### Initialization Issues

| Symptom | Likely Cause | Steps |
| --- | --- | --- |
| `xgl_init()` returns error | Invalid config parameters | Check `xgl_config_validate()` return value |
| Instance creation fails | Memory pool exhausted | Check `TX_POOL_SIZE` and `RX_BUFFER_SIZE` |
| Auth init fails | Missing auth_provider | Ensure provider is provided when `auth_required=true` |

### Send Issues

| Symptom | Likely Cause | Steps |
| --- | --- | --- |
| `xgl_send()` returns `XGL_ERR_WINDOW_FULL` | Sliding window full | Increase `window_size` or wait for ACK |
| `xgl_send()` returns `XGL_ERR_BUFFER_TOO_SMALL` | Payload exceeds MTU, fragmentation disabled | Enable fragmentation or reduce payload |
| No response to send | Route not configured | Check `routes[]` configuration |

### Receive Issues

| Symptom | Likely Cause | Steps |
| --- | --- | --- |
| No data received | PHY callback not registered | Check `phy.rx` callback |
| Data received but not delivered | Authentication failure | Check `auth_required` and auth_provider |
| Out-of-order data | Inconsistent network paths | Check routing and network topology |

## Debugging Tools

### Statistics Counters

```c
xgl_statistics_t stats;
xgl_get_statistics(instance, &stats);
printf("TX frames: %lu, RX frames: %lu\n",
       stats.datalink.tx_frames, stats.datalink.rx_frames);
printf("CRC errors: %lu, Auth failures: %lu\n",
       stats.datalink.rx_crc16_errors, stats.datalink.rx_auth_failures);
```

### Error Callback

```c
void my_error_callback(xgl_error_t error, const char* message, void* user_data) {
    printf("XGL Error %d: %s\n", error, message);
}
```

### Logging

Enable via Kconfig: `XGL_ENABLE_LOGGING=y`, `XGL_LOG_LEVEL_DEBUG=y`.

## Troubleshooting Flow

1. Check error callbacks for reported errors.
2. Analyze statistics counters for anomalies.
3. If data not delivered, check authentication and replay.
4. If no data received, check PHY and routing.

## Common Error Codes

| Code | Meaning | Common Cause |
| --- | --- | --- |
| `XGL_ERR_INVALID_PARAM` | Invalid parameter | Check function arguments |
| `XGL_ERR_NO_MEMORY` | Out of memory | Increase memory pool |
| `XGL_ERR_ROUTE_NOT_FOUND` | Route not found | Check routing config |
| `XGL_ERR_TIMEOUT` | Operation timeout | Check network latency |
| `XGL_ERR_ACK_TIMEOUT` | ACK timeout | Check peer reachability |
| `XGL_ERR_TTL_EXPIRED` | TTL expired | Check route hop count |
| `XGL_ERR_INVALID_FRAME` | Invalid frame | Check PHY quality |
| `XGL_ERR_CRC_FAILED` | CRC failure | Check data integrity |

## Performance Diagnosis

- **High latency**: Check RTT estimator, retransmission rate, window utilization.
- **Low throughput**: Check sliding window size, MTU, PHY speed.
- **Memory issues**: Use tracking allocator, check packet pool peak usage.

## Evidence

| Tool | Source |
| --- | --- |
| Statistics query | `src/api/xgl_stats.c` |
| Error strings | `include/xgl/xgl_error.h` |
| Platform info | `src/platform/xgl_platform.c` |