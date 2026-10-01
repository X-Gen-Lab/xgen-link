# SDK 3 Modular Migration

SDK 3 intentionally removes the compatibility layer around generic utilities and the implicit platform runtime. Rebuild consumers against the new headers and library together.

## Build and Dependencies

The protocol consumes five independent C11 packages, currently version 0.1.0. Each package must satisfy `>=0.1.0,<0.2.0`, ABI 1:

| Package | Targets used by the protocol | Source option for dev only |
| --- | --- | --- |
| xgen_status | `xgs::status` | `XGL_DEV_STATUS_SOURCE_DIR` |
| xgen_bytes | `xgb::bytes` | `XGL_DEV_BYTES_SOURCE_DIR` |
| xgen_crc | `xgcrc::crc16` | `XGL_DEV_CRC_SOURCE_DIR` |
| xgen_memory | `xgm::allocator`, `xgm::pool`, `xgm::size_class`; optional `xgm::libc_allocator` | `XGL_DEV_MEMORY_SOURCE_DIR` |
| xgen_containers | `xgct::list`, `xgct::bitset`; `xgct::hash` when route indexing is enabled | `XGL_DEV_CONTAINERS_SOURCE_DIR` |

The production CMake entry accepts compatible parent-provided targets or installed packages through `find_package` only. The product selects one dependency combination per image; link has removed its five `external` submodules and does not recursively check out a second production set. Both entries check version, ABI, and target type. Configuration neither downloads dependencies nor searches foundation source directories.

The old production `XGL_{STATUS,BYTES,CRC,MEMORY,CONTAINERS}_SOURCE_DIR` arguments now fail explicitly. Standalone source development uses `cmake -S dev` and all five `XGL_DEV_*_SOURCE_DIR` arguments above; merely renaming variables while configuring the root is insufficient. `dev/dependencies.json` pins this repository's development/CI inputs, not product versions. See [Build and test](../getting-started/build-and-test.md) and the repository's `dev/README.md`.

Examples, smoke tests, and release helpers default to disabled when the production module is consumed as a subdirectory; dev explicitly enables development checks. Use a new build directory because old caches can retain removed source paths and helper options.

The installed entry point checks the provided set per package. A complete set is reused and checked against the same compatibility requirements without consulting older installed metadata. A partial set still loads the package, whose exact identity checks reject combinations of different package revisions.

All targets from one package must use the same version, checked by both source and installed entry points. For example, memory allocator 0.1.0 cannot be combined with pool 0.1.1. Different packages can independently select versions within their respective compatibility ranges.

The old core aggregate, `XGL_CORE_SOURCE_DIR`, and `xgc::*` are no longer consumption paths. The public allocator is now `xgm_allocator_t` from `<xgen/memory/allocator.h>`; internal containers, bytes, and CRC use `xgct_`, `xgb_`, and `xgcrc_`. No old-prefix or xgl utility aliases are provided. `xgl_error_t` remains the protocol error domain; generic statuses use `xgs_status_t`. Protocol consumption does not automatically include status strings, CRC8, arena, tracking, or ring_buffer. The protocol PHY contract remains synchronous.

## Compiled Profiles

Select `XGL_PROFILE=boot|embedded|full` through CMake. Generated profile headers and ABI/build identifiers must match the linked binary. Runtime configuration can disable compiled features, but cannot add features absent from the selected profile.

Boot removes optional fields and code at compile time. Compression/encryption registries, internal mutexes, global clocks, and legacy layer-vtable dispatch are not SDK extension points.

## Workspace Migration

Replace old TX-pool and tiered-pool configuration with explicit peer, packet, message, and reassembly capacities. Query `xgl_memory_requirements()` and use `xgl_init_static()` for caller storage. Dynamic creation uses `const xgm_allocator_t*` with `ctx` and makes one backend reservation.

Configuration is borrowed, not copied. A function-local config that goes out of scope while the instance lives is invalid. Keep configuration and referenced descriptors immutable and alive until destroy.

## Runtime and Ownership

Replace implicit-clock send/run functions with `xgl_send_at()`, `xgl_send_zerocopy_at()`, `xgl_step()`, and `xgl_next_timeout()`. Supply one monotonic 32-bit millisecond clock and serialize access externally.

PHY TX is synchronous and may not retain a pointer after returning. An asynchronous DMA driver must copy into driver-owned storage before returning. Receive callbacks borrow bytes only for their call; use `rx_accept_callback` for explicit admission/backpressure. No callback may reenter or destroy its instance.

## Scope and Security Migration

The reliable owner is the complete remote/connection/epoch scope. After failure or explicit close, drain old unauthenticated traffic and reconnect under a new epoch. Authenticated peers are closed through `xgl_close_security_session()`, and trusted directional keys/nonce domains must be refreshed without reuse.

Authentication providers use `xgl_auth_input_t`, including the actual wire identity, nonce, AAD, and payload. Do not reuse an old provider that signs payload alone. Consult the protocol security documentation and validate your trusted-session establishment independently.
