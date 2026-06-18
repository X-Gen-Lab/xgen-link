# Statistics System

XGL's statistics system provides layered protocol stack runtime metrics collection for production debugging and performance monitoring.

## Three-Layer Statistics Structure

```text
xgl_statistics_t (instance-level aggregate)
├── api        (xgl_layer_stats_t — API layer stats)
├── wire       (xgl_layer_stats_t — Wire layer stats)
├── datalink   (xgl_layer_stats_t — Datalink layer stats)
├── network    (xgl_layer_stats_t — Network layer stats)
└── transport  (xgl_layer_stats_t — Transport layer stats)
```

Each `xgl_layer_stats_t` contains:

| Counter | Description |
| --- | --- |
| `tx_frames` | Transmitted frames |
| `rx_frames` | Received frames |
| `tx_bytes` | Transmitted bytes |
| `rx_bytes` | Received bytes |
| `tx_errors` | Transmission errors |
| `rx_errors` | Reception errors |

## Layer-Specific Counters

### Datalink

`rx_header_crc_errors`, `rx_crc16_errors`, `rx_auth_failures`, `rx_replay_duplicates`

### Transport

`tx_retries`, `tx_ack_timeouts`, `tx_window_full`

### Network

`rx_forwarded`, `rx_ttl_expired`, `tx_route_not_found`

## API

### Query Statistics

```c
xgl_statistics_t stats;
xgl_get_statistics(instance, &stats);
```

### Per-Phase Memory Statistics

The tracking allocator provides per-phase memory allocation statistics: INIT, TX, RX, RELIABLE, FRAGMENT.

```c
xgl_allocator_phase_stats_t phase_stats;
xgl_tracking_allocator_get_phase_stats(tracker, &phase_stats);
```

## Compile-Time Control

`XGL_ENABLE_STATISTICS` (default y): When disabled, all counters are eliminated at compile time.

## Evidence

| Rule | Source | Test |
| --- | --- | --- |
| Stats aggregation | `src/api/xgl_stats.c` | `test/test_stats.cpp` |
| Layered stats | `src/api/xgl_stats.c` | `test/test_layered_stats.cpp` |
| Tracking allocator phases | `src/memory/xgl_tracking_allocator.c` | `test/test_allocator.cpp` |
| Tiered pool stats | `src/memory/xgl_tiered_pool_stats.c` | `test/test_tiered_pool.cpp` |