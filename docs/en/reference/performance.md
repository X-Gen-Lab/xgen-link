# Performance and footprint

Measure the linked consumer for the target ABI. Static archive size and host `sizeof` are not MCU resource budgets.

## Resource evidence

The reproducible Cortex-M0 command and measurement scope are in `tools/boot_footprint/README.md`. The report records toolchain flags, source hashes, linked ELF, map and stack-use files. It includes the static workspace and separately reserved stack. It does not measure interrupts, a board Flash driver or stack high-water marks.

## Throughput

The `test/benchmark` applications report host observations. Tune MTU, poll period, RX byte budget and the independent retained-packet limits together. Increasing a window without increasing available retention does not create capacity. Real link speed, loss, driver buffering and Flash latency must be measured on the board.
