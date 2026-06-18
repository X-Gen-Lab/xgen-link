# Testing Strategy

This document describes the XGL protocol stack's test architecture, the design intent of each test category, and coverage strategy.

## Test Layering

```text
┌─────────────────────────────────────────┐
│ Integration Tests (1 file)              │
│  End-to-end protocol stack behavior     │
├─────────────────────────────────────────┤
│ Property-Based Tests (11 files)         │
│  Invariant verification, fuzz, boundary │
├─────────────────────────────────────────┤
│ Unit Tests (30+ files)                  │
│  Per-module functional verification     │
├─────────────────────────────────────────┤
│ Mocks (3 pairs)                         │
│  PHY, callbacks, allocator substitutes  │
└─────────────────────────────────────────┘
```

## Property-Based Testing

XGL uses a custom property-based testing framework (`test/property/property_framework.h`) that verifies protocol stack invariants with randomized inputs.

### Test Files and Verified Invariants

| File | Verified invariant |
| --- | --- |
| `test_alignment_properties.cpp` | Memory alignment: all structs are correctly accessed at target alignment boundaries |
| `test_crc_properties.cpp` | CRC computation: same data always produces same CRC; different data produces different CRC |
| `test_error_properties.cpp` | Error handling: all APIs return explicit error codes for invalid parameters, no crashes |
| `test_fragment_properties.cpp` | Fragment reassembly: any fragment order reassembles consistently; timeout cleanup correct |
| `test_frame_properties.cpp` | Frame encode/decode: encode → decode round-trip consistent; field boundaries handled correctly |
| `test_instance_properties.cpp` | Instance lifecycle: create → run → destroy no leaks; repeated init safe |
| `test_memory_properties.cpp` | Memory allocation: alloc/free paired; pool exhaustion returns NULL; peak stats correct |
| `test_network_properties.cpp` | Network layer: route lookup correct; TTL decremented; forwarding CRC recomputed |
| `test_serialization_properties.cpp` | Serialization: TLV encode/decode round-trip consistent; boundary lengths handled correctly |
| `test_transport_properties.cpp` | Transport: reliable send releases after ACK; retransmission on timeout; window-full blocks |

### Property Test Pattern

Each property test follows:

1. **Define invariant**: Describe the condition expected to hold.
2. **Generate random input**: Use deterministic seeds to generate random parameters.
3. **Execute operations**: Run protocol operations on random inputs.
4. **Verify invariant**: Assert the invariant still holds after operations.
5. **Record seed**: On failure, record the random seed for precise reproduction.

## Mock Design

### mock_phy

Simulates physical layer TX/RX for testing datalink and network layers without hardware:

- `mock_phy_init()`: Initialize mock PHY, configure TX/RX buffers.
- `mock_phy_get_tx_buffer()`: Retrieve transmitted data for frame format verification.
- `mock_phy_enqueue_rx()`: Inject received data, simulating remote transmission.
- `mock_phy_reset()`: Reset state.

### mock_callbacks

Simulates application-layer callbacks:

- `mock_rx_callback`: Records received data for delivery correctness verification.
- `mock_error_callback`: Records errors for error handling verification.

### mock_allocator

Simulates a memory allocator:

- `mock_allocator_init()`: Initialize with configurable failure points.
- `mock_allocator_set_fail_after()`: Set failure after the Nth allocation to test memory exhaustion paths.
- `mock_allocator_get_alloc_count()`: Query allocation count.

## Integration Testing

`test/integration/test_integration.cpp` verifies end-to-end behavior of the complete protocol stack:

1. Create instance + configure routes + register callbacks.
2. Inject received data through mock PHY.
3. Call `xgl_run()` to drive the protocol stack.
4. Verify the application receives correct data.
5. Verify statistics counters are correct.

## Test Build

```bash
# Build tests
cmake --preset dev
cmake --build build-dev --target xgl_tests

# Run tests
ctest --test-dir build-dev --output-on-failure

# Run specific test
./build-dev/test/xgl_tests --gtest_filter="TestName"
```

## Coverage

Enable coverage at build time:

```bash
cmake --preset dev -DENABLE_COVERAGE=ON
cmake --build build-dev
ctest --test-dir build-dev
gcovr build-dev --root .
```

## Traceability

| Component | Source | Tests |
| --- | --- | --- |
| Property framework | `test/property/property_framework.h` | `test/property/test_*.cpp` |
| Mock PHY | `test/mocks/mock_phy.cpp` | `test/integration/test_integration.cpp` |
| Mock callbacks | `test/mocks/mock_callbacks.cpp` | `test/integration/test_integration.cpp` |
| Mock allocator | `test/mocks/mock_allocator.cpp` | `test/integration/test_integration.cpp` |