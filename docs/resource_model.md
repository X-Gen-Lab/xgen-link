# Resource Model

xgen-link is intended for bounded embedded systems. This document separates current behavior from the target production profiles.

## Allocation Phases

The SDK tracks memory in four phases:

- Init phase: `xgl_create` and `xgl_init` allocate instance, route, RX cache, sequence, ACK, reliable, fragment, and pool resources.
- Steady TX/RX phase: normal non-fragmented unreliable traffic should avoid unbounded allocation.
- Reliable phase: reliable TX currently stores retransmission data and may allocate queue nodes and packet copies.
- Fragment phase: fragmentation and reassembly currently allocate fragment arrays, fragment data, reassembly buffers, and bitmaps.

## Current Runtime Allocation Sites

The current implementation still has runtime allocation in some protocol paths:

- datalink TX uses a stack buffer for small frames and heap allocation for larger serialized frames
- reliable send allocates retransmission queue nodes and payload copies
- fragmentation allocates fragment arrays and fragment buffers
- reassembly allocates buffer metadata, received bitmap, and assembled data
- route, sequence, hash table, packet pool, tiered pool, and ACK structures allocate during init

These allocations are acceptable for the host and general SDK profile. They are not yet acceptable for a strict no-runtime-heap MCU profile.

## Production Profiles

### Host Profile

- Default development and CI profile.
- Runtime allocation is allowed when routed through the configured allocator or the default allocator.
- Used for Windows mock, examples, property tests, and install package validation.

### FreeRTOS Profile

- Init-time allocation is allowed during application startup.
- Runtime allocation should be bounded and preferably disabled for real-time tasks.
- Port code should expose stack high-water mark checks with `uxTaskGetStackHighWaterMark`.
- Blocking PHY calls must not run from ISR context.

### Bare-Metal No-Heap Profile

- Target production profile for small MCUs.
- All protocol storage is supplied by static buffers or init-time pools.
- After `xgl_init`, normal acceptance traffic must not call allocator functions.
- Unsupported features must fail configuration validation instead of allocating unexpectedly.

## Measurement Requirements

Each production profile should report:

- target flash bytes from linker map
- static RAM bytes from linker map
- configured TX pool and RX cache bytes
- maximum observed stack depth
- init allocation count and bytes
- runtime allocation count and bytes by traffic phase
- peak reliable queue usage
- peak fragment reassembly usage

## Design Rules

- Public APIs must document buffer ownership and lifetime.
- Reliable TX may copy data because retransmission needs stable storage.
- Single-frame unreliable zero-copy TX frames data in the caller buffer and passes that buffer directly to PHY.
- Compression and encryption must declare maximum output expansion before they are allowed in production traffic.
- Any future dynamic allocation path must be covered by tests and reflected in the footprint report.
