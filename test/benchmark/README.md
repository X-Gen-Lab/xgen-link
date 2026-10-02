# Host protocol benchmarks

Configure with `XGL_BUILD_TESTS=ON` and `XGL_BUILD_BENCHMARKS=ON`, then build the
`xgl_benchmarks` target. The three executables print CSV and exit nonzero if an
exchange does not complete.

`benchmark_support.h` owns both configurations, route/PHY descriptors, callbacks,
and handles until teardown. Every exchange uses explicit logical milliseconds,
checks send acceptance, drains bounded RX chunks until actual application
delivery, and processes the returning ACK for reliable data. Callbacks never
reenter the instance. Host wall-clock timing uses `std::chrono::steady_clock`.

- `bench_throughput` measures completed unreliable exchanges for three payload sizes.
- `bench_latency` measures host CPU time through a reliable delivery and ACK processing.
- `bench_memory` queries exact static workspace bytes per preset and separately
  reports protocol memory-accounting fields. With statistics enabled,
  `memory_used` and `memory_peak` record the complete reserved workspace, including
  the initial reservation; resetting statistics preserves that reservation. They
  are not live pool occupancy or allocator tracking counters. Profiles without
  statistics return `XGL_ERR_UNSUPPORTED` instead of a zero-usage measurement.

These are host simulation measurements, not UART/radio bandwidth, MCU scheduling
latency, Flash size, or worst-case stack measurements. Compiler settings, ABI,
profile, and application resource caps affect the results. Use the board linker
map and stack instrumentation for target resource acceptance.
