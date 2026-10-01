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
  reports protocol memory-accounting fields. A libc allocator without a tracking
  adapter reports zero for the latter fields; zero does not mean zero RAM usage.

These are host simulation measurements, not UART/radio bandwidth, MCU scheduling
latency, Flash size, or worst-case stack measurements. Compiler settings, ABI,
profile, and application resource caps affect the results. Use the board linker
map and stack instrumentation for target resource acceptance.
