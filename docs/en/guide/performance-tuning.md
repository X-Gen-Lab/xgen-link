# Performance Tuning Guide

This document provides performance optimization strategies, configuration tuning recommendations, and best practices for the XGL protocol stack.

## Performance Factors

```text
Performance = min(PHY speed, CPU capability, memory bandwidth, protocol overhead)
```

Key factors:

1. **PHY speed**: Physical transmission capability is the upper bound.
2. **CPU capability**: Encoding/decoding, CRC calculation, encryption.
3. **Memory allocation**: Pool exhaustion leads to allocation failures.
4. **Protocol overhead**: Header, CRC, authentication tag.

## Configuration Tuning

### Sliding Window

| Window Size | Use Case | Throughput Impact |
| --- | --- | --- |
| 2 | Low latency, low bandwidth | Low |
| 4 | Balanced (default) | Medium |
| 8 | High bandwidth, high latency | High |
| 16 | Very high bandwidth | Highest |

**Formula**: `max throughput ≈ (window_size × MTU) / RTT`

### Timeout Settings

| Parameter | Default | Recommendation |
| --- | --- | --- |
| `ACK_TIMEOUT_MS` | 100-1000 | Adjust based on RTT, typically 2×RTT |
| `MAX_RETRY` | 3-5 | Increase for poor link quality |
| `REASSEMBLY_TIMEOUT_MS` | 5000 | Fragment reassembly timeout |

## Code-Level Optimization

### Reduce Copies

1. Use zero-copy send: `xgl_send_zerocopy()`.
2. Avoid unnecessary payload copies.
3. Use reference counting to share data.

### Reduce Lock Contention

1. Single-thread mode: `XGL_THREAD_SAFE=n`.
2. Reduce `xgl_send()` call frequency; batch sends.
3. Use separate instances to avoid sharing.

### Reduce CRC Computation

1. Increase MTU to reduce frame count.
2. Enable hardware CRC (if supported).

## Monitoring Recommendations

1. **Real-time monitoring**: Use statistics API for key metrics.
2. **Alert setup**: Set alerts for abnormal counters.
3. **Performance analysis**: Regular performance benchmarking.

## Best Practices

1. **Pre-allocate memory**: Use no-heap profile to avoid dynamic allocation.
2. **Reasonable configuration**: Choose appropriate presets for your scenario.
3. **Avoid over-optimization**: Ensure correctness first, then optimize performance.
4. **Test validation**: Any optimization must be validated through testing.

## Evidence

| Rule | Source | Tool |
| --- | --- | --- |
| Performance stats | `src/api/xgl_stats.c` | `test/test_stats.cpp` |
| Resource presets | `Kconfig` | `tools/footprint_report.cmake` |