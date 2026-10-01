# xgen-link SDK 3

Portable C11 protocol layers with explicit resource limits. Generic capabilities come from independent xgen-status, xgen-bytes, xgen-crc, xgen-memory, and xgen-containers packages. See [Build and test](getting-started/build-and-test.md) for preparation.

## Architecture

Static and allocator initialization share one workspace layout and state machine. The application supplies time, serializes instance access and implements synchronous PHY callbacks. Wire v3 separates reliable DATA numbering from authentication sequencing.

## Start here

- [Build and test](getting-started/build-and-test.md)
- [Quick start](getting-started/quick-start.md)
- [Migration](guide/modular-migration.md)
- [Public API](reference/public-api.md)
- [Architecture](protocol/architecture.md)
- [Validation matrix](reference/validation-matrix.md)

## Product boundary

Boot excludes authentication, fragmentation, forwarding and out-of-order retention. Embedded/Full provide those bounded capabilities. Compression, payload encryption and asynchronous DMA ownership are not implemented. PHY TX must finish consuming or copying bytes before returning. Board integration and production authentication require application-specific validation.
