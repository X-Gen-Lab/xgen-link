# Configuration

Start with a preset compatible with the compiled profile, fill in application details, then call `xgl_config_validate()` and `xgl_memory_requirements()` before allocating storage. Presets are starting capacities, not measured MCU RAM guarantees.

## Lifetime and Identity

The instance borrows the immutable `xgl_config_t` until `xgl_destroy()`. Keep its route array, PHY descriptors, allocator service, authentication provider, name, and callback contexts alive as well. Do not change configuration after creation or overlap it with the workspace.

Set a nonzero `source_id` other than `XGL_BROADCAST_ID`. Routes determine reachable destinations. A receive handler is needed to consume application data; `rx_accept_callback` takes precedence over the legacy void `rx_callback`.

## Routes and Frames

| Field | Meaning |
| --- | --- |
| `target_id` | Destination node |
| `phy` | Borrowed synchronous TX and nonblocking RX callbacks |
| `max_frame_size` | Complete serialized frame limit for this route |
| `read_freq_hz` | Link polling frequency |
| `metric` | Route selection metric |

Routes have fixed capacity established at initialization. Routes sharing the same PHY descriptor share a parser/link; distinct PHY descriptors have independent RX state.

The effective frame must fit the configured protocol limit and route MTU, including the 24-byte base header, extensions, payload, authentication tag if enabled, and 2-byte CRC. `memory.rx_buffer_size` must be at least `protocol.max_frame_size`.

## Explicit Capacities

| Configuration field | Bound |
| --- | --- |
| `features.max_peers` | Simultaneous connection/epoch scopes |
| `protocol.window_size` | Outstanding reliable packets per peer, 1–32 |
| `features.max_tx_packets` | Outstanding reliable packets across the instance |
| `features.max_rx_buffered_packets` | Out-of-order packets across the instance |
| `features.max_message_size` | Largest fragmented TX/RX message |
| `features.max_reassembly_slots` | Concurrent incomplete reassemblies |
| `features.max_reassembly_bytes` | Shared incomplete and completed-but-pending RX bytes |
| `features.max_tx_message_bytes` | Shared retained message bytes waiting for TX pumping |

Enabled capacities are explicit; zero does not mean unlimited. Peer and TX packet capacities must be nonzero. Window sizes above one require nonzero out-of-order capacity. Fragmentation requires nonzero message size, reassembly slots, and both message byte budgets. Disabled optional resources can remain zero. A smaller global TX budget than `max_peers * window_size` is valid and causes earlier backpressure.

Byte budgets restrict admission; the workspace reserves fixed maximum-size slots to avoid fragmentation. Lowering a byte budget alone does not necessarily reduce reserved RAM. See [Resource Model](resource-model.md).

## Authentication

An authentication-capable profile requires an application-supplied `auth_provider` when `auth_required` is true. Supply synchronous sign/verify callbacks and a tag length in `1..XGL_AUTH_TAG_MAX_LEN`. Install trusted directional parameters with `xgl_install_security_session()` before sending authenticated traffic.

Authentication does not require a libc allocator: static workspaces support it. Session keys, prefixes, epochs, and restart freshness are application responsibilities. Ordinary packets cannot create trusted sessions. Closing a session preserves a nonce-domain tombstone.

## Validation and Build Profiles

`boot` permits one peer, at most one route, window one, and no authentication, fragmentation, forwarding, or out-of-order buffering. `embedded` and `full` compile these capabilities; runtime configuration still determines enabled features and capacities.

Compression and payload encryption remain reserved and rejected; keep both feature flags false and `compression_id` zero. Use `xgl_memory_requirements()` to detect workspace arithmetic overflow and query the actual ABI-dependent size. Build and consume matching generated profile headers.
