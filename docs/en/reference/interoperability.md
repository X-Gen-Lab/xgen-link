# Interoperability Testing

This document describes XGL protocol stack interoperability testing strategies, cross-platform verification methods, and multi-implementation compatibility guarantees.

## Interoperability Goals

Ensure protocol compatibility across implementations, platforms, and compilers:

1. **Cross-platform**: ARM Cortex-M, x86, RISC-V, etc.
2. **Cross-compiler**: GCC, Clang, MSVC, IAR, Keil.
3. **Cross-OS**: FreeRTOS, Zephyr, Linux, Windows, bare-metal.
4. **Cross-version**: Backward compatibility between v1.0 and future versions.

## Testing Layers

```text
┌─────────────────────────────────────┐
│ Conformance Tests                    │
│  Verify implementation conforms      │
│  to protocol specification           │
├─────────────────────────────────────┤
│ Cross-Platform Tests                 │
│  Verify data exchange between        │
│  different platforms                 │
├─────────────────────────────────────┤
│ Inter-Implementation Tests           │
│  Verify interoperability between     │
│  different implementations           │
└─────────────────────────────────────┘
```

## Protocol Conformance

### Wire Format Conformance

1. **Encoding**: All implementations produce identical wire format output for identical input.
2. **Decoding**: All implementations correctly decode specification-compliant frames.
3. **Boundary conditions**: Maximum/minimum field values, edge alignment.

### State Machine Conformance

1. **State transitions**: All implementations follow identical transition rules.
2. **Timeout behavior**: Timeout values and handling logic are consistent.
3. **Error handling**: Error codes and recovery strategies are consistent.

## Cross-Platform Testing

### Endianness

Verify little-endian wire format works correctly between:
- Little-endian platforms (x86, ARM)
- Big-endian platforms (PowerPC, MIPS)

### Alignment

Verify strict-alignment platforms (Cortex-M0, MIPS) and relaxed-alignment platforms (x86, Cortex-M3/M4) handle all structures correctly.

### Pointer Size

Verify 32-bit (Cortex-M3, RV32) and 64-bit (x86-64, RV64) platforms interoperate correctly.

## Inter-Implementation Testing

### Methods

1. **Loopback**: Send and receive within the same implementation.
2. **Cross-test**: Send and receive between different implementations.
3. **Multi-hop**: Forward through intermediate nodes.

### Test Cases

| Case | Description | Verification |
| --- | --- | --- |
| Basic send | Unreliable single-frame | Data integrity |
| Reliable send | ACK confirmation and retransmission | Delivery reliability |
| Fragmented transfer | Large data fragmentation/reassembly | Data consistency |
| Authenticated frames | Frames with auth tags | Authentication verification |
| Forwarding | Multi-hop routing | TTL decrement and CRC recalculation |

## Version Compatibility

### Wire Format Version

Current: v2

Strategy:
1. New versions must decode old version frames.
2. Old versions reject new version frames with `XGL_ERR_INVALID_VERSION`.
3. Version field in frame header enables quick identification.

### API Version

Public API stability:
1. New APIs don't remove old APIs.
2. Deprecated APIs marked as `deprecated`.
3. Major version changes clean up deprecated APIs.

## Known Limitations

| Limitation | Description | Impact |
| --- | --- | --- |
| No runtime version negotiation | Version mismatch rejects directly | Requires pre-coordination |
| No self-describing format | Fixed field offsets | Version changes require negotiation |
| No feature negotiation | Fixed feature set | Different configurations may be incompatible |

## Evidence

| Rule | Source | Test |
| --- | --- | --- |
| Wire format encode/decode | `src/wire/xgl_wire.c` | `test/test_wire.cpp` |
| Endianness handling | `include/xgl/internal/xgl_platform.h` | `test/test_platform.cpp` |
| Version check | `src/api/xgl_version.c` | `test/test_instance.cpp` |