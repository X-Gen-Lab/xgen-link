# Footprint Report

The build provides an `xgl_footprint` target that writes `build/<preset>/footprint/xgl-footprint.txt`.

The report contains:

- static library archive size as a host-side flash proxy
- preset RAM configuration for TX pool and RX buffer
- explicit stack buffers used in the portable C implementation
- dynamic allocation measurement source

Host archive size is not a substitute for target flash size. For MCU release builds, pair this report with the linker map file and compiler stack usage output.

## Dynamic Allocation Count

`test/test_footprint.cpp` uses a counting allocator around `xgl_create`, `xgl_init`, and `xgl_destroy`. The test checks that allocations and frees balance, and records the init-time allocation count used by the default tiny SDK profile.

## Stack Measurement

The portable implementation has explicit stack buffers in datalink TX/RX. Target builds should also enable compiler stack usage output or RTOS high-water mark checks:

- GCC/Clang: `-fstack-usage`
- FreeRTOS: `uxTaskGetStackHighWaterMark`
- Bare metal: linker map plus sentinel fill of stack region
