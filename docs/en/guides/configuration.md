# Configuration

Start with a preset compatible with the compiled profile, fill in application details, then call `xgl_config_validate()` and `xgl_memory_requirements()` before allocating storage. Presets are starting capacities, not measured MCU RAM guarantees.

`xgl_config_get_default()` uses the Boot preset when fragmentation is not compiled; other profiles use the Medium template and limit `protocol.window_size` to the compiled window capacity. Named presets and `XGL_CONFIG_PRESET_*` macros retain their fixed templates without automatically reducing windows or other capacities. For example, a build with window capacity one can use the default configuration, while Tiny still has a window template of two and needs explicit adjustment before validation.

## Lifetime and Identity

The instance borrows the immutable `xgl_config_t` until `xgl_destroy()`. Keep its route array, PHY descriptors, allocator service, authentication provider, name, and callback contexts alive as well. Do not change configuration after creation or overlap it with the workspace.

Set a nonzero `source_id` other than `XGL_BROADCAST_ID`. Routes determine reachable destinations. A receive handler is needed to consume application data; `rx_accept_callback` takes precedence over the legacy void `rx_callback`.

## Routes and Frames

| Field | Meaning |
| --- | --- |
| `target_id` | Destination node |
| `phy` | Borrowed synchronous TX and nonblocking RX callbacks |
| `max_frame_size` | Complete serialized frame limit for this route |
| `read_freq_hz` | Owner-driven read frequency hint; creates no timer or task |
| `metric` | Reserved metric; not currently used for path selection |

Routes have fixed capacity established at initialization. Routes sharing the same PHY descriptor share a parser/link; distinct PHY descriptors have independent RX state.

Actual polling also depends on step frequency, byte limits and callback duration. A shared PHY uses the highest frequency hint among its routes; the protocol cannot guarantee throughput that the scheduler does not provide.

Select the temporary PHY read chunk through CMake `XGL_DATALINK_RX_CHUNK_SIZE`: AUTO defaults to 32 for Boot and 128 elsewhere, with explicit 16/32/64/128 also supported, for example `-DXGL_DATALINK_RX_CHUNK_SIZE=64`. It bounds the per-read stack buffer without changing MTU or persistent RX storage. The generated header records the actual value; consumers of an installed library cannot inject a conflicting macro. See [Datalink](../explanation/datalink.md) for throughput and timeout considerations.

The effective frame must fit the configured protocol limit and route MTU, including the 24-byte base header, extensions, payload, authentication tag if enabled, and 2-byte CRC. `memory.rx_buffer_size` must be at least `protocol.max_frame_size`.

## Explicit Capacities

| Configuration field | Bound |
| --- | --- |
| `features.max_peers` | Simultaneous connection/epoch scopes |
| `protocol.window_size` | Outstanding reliable packets per peer, 1–32, within compiled window capacity |
| `features.max_tx_packets` | Outstanding reliable packets across the instance |
| `features.max_rx_buffered_packets` | Out-of-order packets across the instance |
| `features.max_message_size` | Largest fragmented TX/RX message |
| `features.max_reassembly_slots` | Concurrent incomplete reassemblies |
| `features.max_reassembly_bytes` | Shared incomplete and completed-but-pending RX bytes |
| `features.max_tx_message_bytes` | Shared retained message bytes waiting for TX pumping |

Enabled capacities are explicit; zero does not mean unlimited. Peer and TX packet capacities must be nonzero. Window sizes above one require nonzero out-of-order capacity. Fragmentation requires nonzero message size, reassembly slots, and both message byte budgets. Disabled optional resources can remain zero. A smaller global TX budget than `max_peers * window_size` is valid and causes earlier backpressure.

With fragmentation enabled, `features.max_message_size` must fit the wire message-length field's `UINT32_MAX` and must not exceed `features.max_reassembly_bytes`; configuration validation and workspace measurement reject violations directly. With fragmentation disabled, this message-length field does not affect validation, storage reservations, or initialization limits.

Byte budgets restrict admission; the workspace reserves fixed maximum-size slots to avoid fragmentation. Lowering a byte budget alone does not necessarily reduce reserved RAM. See [Resource Model](../explanation/resource-model.md).

With explicit `XGL_ENABLE_ADVANCED_RUNTIME=ON`, a separate versioned descriptor can reduce whole-message slots and configure absolute deadlines and scoped receive callbacks; see [advanced runtime options](advanced-runtime.md). Default OFF retains the existing configuration ABI. Installed consumers can require the `advanced_runtime` capability.

## Authentication

An authentication-capable profile requires every nonnull `auth_provider` to supply synchronous sign/verify callbacks and a tag length in `1..XGL_AUTH_TAG_MAX_LEN`, even when `auth_required` is false. When `auth_required` is true, the provider must also be nonnull. Configuration validation and workspace measurement reject invalid providers, so creation does not allocate a workspace first. Install trusted directional parameters with `xgl_install_security_session()` before sending authenticated traffic.

When `auth_required` is true, `protocol.max_frame_size` must support basic authenticated communication by the local endpoint. Validation uses the wire encoder and frame measurement to check one complete authenticated ordinary `ACK_RANGE` frame, which also covers the base HELLO and minimum DATA at epoch zero. The current base threshold for a 32-byte tag is 88 bytes; a 64-byte protocol limit is rejected before allocation.

This threshold excludes optional `SESSION`, application `DATA_TYPE`, and `FRAGMENT` extensions and does not guarantee that every metadata combination can be sent. An individual route can have a smaller MTU for forwarding or other frames that fit; local transmission and forwarding still check the actual length of each frame. For example, a protocol limit of 128, route MTU of 64, and tag length of 32 permits initialization and short-frame forwarding, but cannot send local authenticated DATA over that route. `auth_required` controls local authenticated communication and does not add endpoint authentication to nonlocal forwarded frames.

Authentication does not require a libc allocator: static workspaces support it. Session keys, prefixes, epochs, and restart freshness are application responsibilities. Ordinary packets cannot create trusted sessions. Closing a session preserves a nonce-domain tombstone.

## Validation and Build Profiles

Only these explicit capability combinations are supported, rather than arbitrary feature-flag permutations. Runtime configuration still selects enabled features and capacities, but cannot restore capabilities removed at build time.

| Profile | Default configuration ID | Authentication/forwarding | Fragmentation/out-of-order | Statistics/diagnostic text |
| --- | ---: | --- | --- | --- |
| `boot` | 301 | No | No | No |
| `embedded-lite` | 304 | No | Yes | Yes |
| `embedded` | 302 | Yes | Yes | Yes |
| `full` | 303 | Yes | Yes | Yes |

Boot permits one peer, at most one route, and window one. Embedded Lite defaults to eight reliable-index buckets and retains no security-session storage. It serves endpoints requiring bounded fragmentation and out-of-order buffering without authentication or forwarding. The generated `xgl_build_config.h` declares the other capacity limits.

Reliable index buckets, private window capacity and adaptive RTO can be selected through controlled compiled options. Default private capacity remains 128 slots, and adaptive RTO defaults to enabled. Runtime settings cannot enlarge compiled capacity. The table's IDs apply only when all resolved resources match profile defaults; changing the resource combination, PHY read chunk or libc fallback produces a different build identity. See [build and test](build-and-consume.md#compiled-resource-options) for options, installed consumption and conflict rejection.

Installed packages export `xgl_PROFILE`, `xgl_BUILD_CONFIG_ID`, `xgl_RESOURCE_CONFIG_ID`, `xgl_CAPABILITIES` and resolved resource options. Target `xgl::xgl` carries the corresponding profile, build identity, resource identity and capability properties. Consumers can require capabilities with `find_package(xgl CONFIG REQUIRED COMPONENTS fragmentation out_of_order)`; unsupported required capabilities fail configuration. Requestable names are `wire`, `reliable`, `authentication`, `forwarding`, `fragmentation`, `out_of_order`, `statistics`, `diagnostics`, and `advanced_runtime`.

Compression and payload encryption remain reserved and rejected; keep both feature flags false and `compression_id` zero. Use `xgl_memory_requirements()` to detect workspace arithmetic overflow and query the actual ABI-dependent size. The library, CMake target and consumer must use matching generated profile and resource headers. Public checked entry points use the generated build identity rather than hardcoded default profile IDs.
