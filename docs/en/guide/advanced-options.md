# Advanced Runtime Options

Build with `-DXGL_ENABLE_ADVANCED_RUNTIME=ON`; installed consumers can require `COMPONENTS advanced_runtime`. Every profile defaults to OFF, and disabled entries return `XGL_ERR_UNSUPPORTED`. This option changes private layout and build identity. Consumers must use the library's generated configuration header.

## Creation and Lifetime

```c
xgl_config_t config;
xgl_config_get_preset_boot(&config);
/* Populate routes, PHY callbacks and product identity before measurement. */
xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
options.tx_lifetime_ms = 1000U;
xgl_memory_report_t report;
xgl_error_t error = xgl_memory_report_with_options(&config, &options, &report);
```

Check the result, provide `report.requirements.size` bytes with its required alignment, then call `xgl_init_static_with_options()`. A product can measure its layout during the build and provide a fixed array, or use an explicit allocator with `xgl_create_with_options()` followed by `xgl_init()`. The static entry already initializes the instance.

Configuration, options, routes, PHY, provider and callback contexts remain valid until destruction; they must not overlap the workspace. Initialize descriptor size, version, configuration ABI and build identity using the macro. Existing creation entries and `xgl_step()` remain available.

## Deadlines and Observation

| Option or event | Meaning |
| --- | --- |
| `reassembly_timeout_ms` | Absolute deadline from the first fragment; zero uses 5,000 ms |
| `tx_lifetime_ms` | Scope reliable TX busy-period limit; zero disables it |
| `rx_scoped_callback` | Precedes accept/legacy callbacks and carries the receive scope |
| `scope_event_callback` | Receives failure and reliable-drain transitions |
| `XGL_SCOPE_TX_DRAINED` | Reliable packets and the message tail in this scope have drained |
| `XGL_SCOPE_FAILED` | Terminal scope failure, accompanied by its error |

Nonzero intervals must be below `2^31` ms. The TX busy period starts when the first reliable work is admitted and includes the full retained message and final unacknowledged packet. Further sends, BUSY, retries and partial ACKs do not extend it. Expiry clears work owned by that scope and reports a timeout. Continuous producers must allow a sufficient busy-period limit or stop admission at explicit transaction boundaries.

A scope with an expired TX busy period terminates before new DATA delivery; receiving before maintenance cannot extend its lifetime. Reassembly time starts at the first admitted fragment, including a cached and SACKed out-of-order fragment. Filling an earlier packet gap, entering reassembly, duplicates and later fragments do not restart it. Expired reliable fragments fail their scope; unreliable expiry releases only the corresponding message.

Protocol drain does not prove remote motor execution, Flash persistence or business success. Use a request ID and application response where business confirmation is required. Closing a peer is a local lifecycle operation; remote notification, reset and retry policy belong to the application.

A scoped receive callback can return BUSY. A completed message retains its connection/epoch until the owner retries after capacity returns. The application needs a wakeup mechanism; protocol timers cannot detect changing application capacity. All callbacks are synchronous and must not reenter or destroy the instance.

## Message Slots and Resource Snapshots

With fragmentation enabled, legacy TX message slots equal `max_peers`, and RX slots equal `max_peers + max_reassembly_slots`. Nonzero `tx_message_slots` and `rx_message_slots` can reduce those counts; zero preserves them. Counts cannot exceed their legacy bounds or be specified when fragmentation is disabled.

The Small template has two peers, four reassembly slots and 1,024-byte messages. Selecting one TX slot and two RX slots removes five complete message blocks, saving 5,120 B. Peer, packet, initialization and management storage still contributes to the total. Reduced slots reject concurrent large messages earlier, so test the actual product workload.

`xgl_resources_get()` reports peers/failed peers, unacknowledged packets and payload, TX messages, out-of-order packets, reassembly and pending deliveries. `reassembly_bytes` includes completed messages awaiting application acceptance; `reassembly_slots` counts active reassembly only. Reading performs no allocation, requires serialization and costs work proportional to configured capacities.

Snapshots describe active content; `xgl_memory_report_with_options()` describes reserved workspace. Statistics `memory_used` still refers to reserved workspace. RX cache can explicitly use `rx_buffer_size = max_frame_size`; named presets preserve their historical margin, and the extra 32 B is not a protocol requirement.

## Security Scope Policy

Profiles with authentication support can set `protect_installed_scopes=true`. It requires a valid provider and a protocol MTU that fits a basic authenticated frame. Install trusted parameters for the exact `(remote_id, connection_id, epoch)` before sending. HELLO, DATA, ACK, SACK, RESET, retries and zero-copy layouts then share this authentication policy.

Closing the scope does not downgrade it to plaintext. Other uninstalled scopes can remain plaintext when global `auth_required=false`; this is neither negotiation nor dynamic trust establishment. Global required mode still protects all local traffic. TX capacity conservatively reserves the tag, so plaintext scopes in mixed mode may also use smaller payloads.

Installation does not revoke previously admitted data; earlier cached plaintext may still be delivered afterward. Use a new epoch/scope and install before receiving when establishing a new trust boundary, and explicitly dispose of old-scope data. See the [security lifecycle](../protocol/security.md).

Closing a security session retains its nonce-domain record; repeated installation and closure cannot reclaim slots indefinitely. The product owns key rotation, epoch and reboot freshness, the real cryptographic provider and its validation. Authentication does not imply payload encryption.

## Scheduling and Validation

See [bounded receive and runtime](bounded-runtime.md) for limits, pending and a DMA owner example. Work-count limits do not replace whole-step WCET measurements. Legacy step uses unlimited candidate and maintenance quotas in an advanced build; do not mix entries where bounded execution is required.

Host GoogleTest verifies behavior. Profile ELFs, stack, DMA cache coherency, interrupt latency and product RAM budgets require separate validation. A footprint that enables the feature but calls only legacy APIs does not measure the complete advanced API call graph.
