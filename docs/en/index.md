# XGL Documentation

XGL is a production-oriented reliable multi-node protocol stack for resource-constrained MCUs. It provides a v2 wire format, multi-PHY routing, reliable delivery, ACK ranges, SACK, authentication, bounded fragment reassembly, and low-power runtime deadlines.

## Current Capabilities

- 24-byte fixed wire header with explicit offset-based encoding.
- 16-bit node IDs and 32-bit packet numbers.
- TLV extensions for session, ACK range, SACK, fragment, security, and route metadata.
- Connection-scoped peer state keyed by `target_id + connection_id + session_epoch`.
- Production authentication provider requirements, including authenticated zero-copy behavior.
- `xgl_next_deadline_ms()` for bare-metal and RTOS sleep scheduling.

## Boundaries

- Single-frame payload length is still limited by `uint16_t payload_len`.
- Large messages use FRAGMENT_EXT.
- Compression and encryption are reserved codec capabilities and are rejected by the production path until fully wired.
- Broadcast and multicast address ranges are reserved; the current reliable path is unicast-focused.

## Recommended Reading

### SDK Users

1. [Quick Start](getting-started/quick-start.md)
2. [Configuration](guide/configuration.md)
3. [Send API](guide/send-api.md)
4. [Zero-Copy](guide/zero-copy.md)
5. [Low-Power Runtime](guide/low-power-runtime.md)

### Protocol Maintainers

1. [Architecture](protocol/architecture.md)
2. [Implementation Map](protocol/implementation-map.md)
3. [State Machines](protocol/state-machines.md)
4. [Wire Format](protocol/wire-format.md)
5. [TLV Extensions](protocol/extensions.md)
6. [Reliability](protocol/reliability.md)
7. [Security](protocol/security.md)
8. [Validation Matrix](reference/validation-matrix.md)

### MCU Release Owners

1. [Porting](guide/porting.md)
2. [Resource Model](guide/resource-model.md)
3. [Production Checklist](guide/production-checklist.md)
4. [Release Validation](reference/release-validation.md)
5. [Static Analysis](reference/static-analysis.md)
