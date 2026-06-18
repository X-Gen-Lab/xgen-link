# Performance and Resource Analysis

This document provides XGL protocol stack performance benchmarks, resource consumption analysis, and optimization recommendations.

## Flash/RAM Footprint

Generate reports using `tools/footprint_report.cmake`:

```bash
cmake --preset release
cmake --build build-release --target xgl_footprint_report
```

### Typical Footprint (ARM Cortex-M4, GCC -O2)

| Profile | Flash | RAM | Use Case |
| --- | --- | --- | --- |
| Tiny (no frag, no auth) | ~8 KB | ~2 KB | Minimum MCU |
| Medium (default) | ~15 KB | ~4 KB | Typical MCU |
| Large (full features) | ~25 KB | ~8 KB | High-end MCU/gateway |

### Module Breakdown

| Module | Flash Share | Description |
| --- | --- | --- |
| Wire (encode/decode/parser) | ~25% | Frame processing core |
| Transport (reliable/fragment) | ~30% | Reliable delivery + fragmentation |
| Network (route/forward) | ~10% | Route lookup and forwarding |
| Datalink (auth/replay) | ~15% | Auth verification + anti-replay |
| Memory (pools/allocator) | ~10% | Memory management |
| Platform (mutex/time) | ~5% | Platform abstraction |
| API + Stats | ~5% | Public interface |

## Throughput

### Single-Hop Send Latency

| Payload Size | Unreliable | Reliable | Notes |
| --- | --- | --- | --- |
| 32 bytes | ~50 μs | ~80 μs | No fragmentation |
| 128 bytes | ~60 μs | ~100 μs | No fragmentation |
| 512 bytes | ~80 μs | ~130 μs | May fragment |

Conditions: ARM Cortex-M4 @ 160MHz, `XGL_THREAD_SAFE=n`.

### Maximum Throughput

- Unreliable single-frame: ~20,000 frames/sec (PHY dependent)
- Reliable single-frame: ~5,000 frames/sec (window=8, RTT=2ms)
- Fragmented transfer: limited by reassembly buffers and timeout

## Resource Presets

| Profile | TX Pool | RX Buffer | Window | Max Retry | Target |
| --- | --- | --- | --- | --- | --- |
| Tiny | 1024 | 128 | 2 | 2 | 64KB Flash MCU |
| Small | 2048 | 256 | 4 | 3 | 128KB Flash MCU |
| Medium | 4096 | 512 | 4 | 3 | 256KB Flash MCU |
| Large | 8192 | 1024 | 8 | 5 | 512KB+ MCU |
| X-Large | 16384 | 2048 | 16 | 5 | Desktop/gateway |

## Optimization Tips

### Reduce Flash

1. Disable unused features: `FRAGMENTATION=n`, `LOGGING=n`.
2. Use Tiny footprint profile.
3. Disable statistics: `XGL_ENABLE_STATISTICS=n`.
4. Disable assertions: `XGL_ENABLE_ASSERTIONS=n`.

### Reduce RAM

1. Smaller TX Pool and RX Buffer.
2. Smaller sliding window: `XGL_DEFAULT_WINDOW_SIZE=2`.
3. Fewer instances: `XGL_MAX_INSTANCES=1`.
4. No-heap profile: `XGL_ALLOW_FALLBACK_MALLOC=0`.

### Increase Throughput

1. Larger sliding window for more in-flight packets.
2. Enable QoS for priority scheduling.
3. Optimize PHY layer callbacks.
4. Smaller auth tag to reduce frame overhead.

## Evidence

| Rule | Source | Tool |
| --- | --- | --- |
| Footprint report | `tools/footprint_report.cmake` | CMake target |
| No-heap smoke | `tools/noheap_smoke.c` | CMake target |
| Resource presets | `Kconfig`, `xgl_config.h` | Compile-time config |