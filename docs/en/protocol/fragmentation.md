# Fragmentation

Fragment metadata is carried in FRAGMENT_EXT, not in a payload prefix.

## Reassembly Key

```text
source_id + connection_id + session_epoch + message_id
```

The same `message_id` in different sessions does not collide.

## Range Model

The reassembly manager tracks received byte ranges or chunk ranges, avoiding per-byte scans for large payloads. Duplicate ranges may be ignored; conflicting or out-of-bounds ranges fail closed.

## Sender Side

The sender computes per-fragment payload capacity from route MTU, base header, extensions, authentication trailer, and frame CRC. Every fragment carries FRAGMENT_EXT and keeps the same `message_id`, `message_len`, connection, and session.

### Fragment Budget Formula

The send planner derives the payload budget in two stages:

```text
app_payload_budget =
  route_max_frame_size
  - XGL_WIRE_BASE_HEADER_SIZE
  - app_extensions_len
  - auth_overhead
  - XGL_CRC16_SIZE

fragment_payload_budget =
  app_payload_budget - XGL_FRAGMENT_EXT_SIZE
```

`app_extensions_len` includes DATA_TYPE_EXT when `data_type != 0`. `auth_overhead`
is SECURITY_EXT plus the provider tag length when authentication is enabled.
When `data_len <= app_payload_budget`, the packet is sent as a single frame.
When fragmentation is needed, every fragment spends additional TLV space on
FRAGMENT_EXT, so `fragment_payload_budget` is smaller than the single-frame
payload budget.

| Budget item | Source | Failure |
| --- | --- | --- |
| Route MTU | `xgl_route_item_t.max_frame_size` | `XGL_ERR_BUFFER_TOO_SMALL` or fragmentation planning failure |
| Base header | `XGL_WIRE_BASE_HEADER_SIZE` | Fixed 24 bytes |
| DATA_TYPE_EXT | `XGL_DATA_TYPE_EXT_SIZE` when data type is non-zero | Included in both single-frame and fragmented sends |
| FRAGMENT_EXT | `XGL_FRAGMENT_EXT_SIZE` | Required only on fragments |
| Auth overhead | `XGL_SECURITY_EXT_SIZE + tag_len` | Requires valid provider/tag length |
| Frame CRC | `XGL_CRC16_SIZE` | Always present |

## Receiver Side

The receiver validates the frame first, then finds a buffer by reassembly key. Only after ranges cover `[0, message_len)` is the assembled payload delivered to transport.

### Reassembly Rules

| Condition | Behavior | Evidence |
| --- | --- | --- |
| First valid fragment for a key | Allocate a reassembly buffer and reserve `message_len` bytes | `src/transport/xgl_fragment_reassembly.c`, `test/test_fragment.cpp` |
| Non-overlapping range | Copy bytes and insert/merge received range | `src/transport/xgl_fragment_range.c`, `test/test_fragment.cpp` |
| Duplicate range | Accepted only when it does not create an invalid overlap | `src/transport/xgl_fragment_range.c`, `test/test_fragment.cpp` |
| `message_len == 0` | Reject | `src/transport/xgl_fragment_process.c` |
| `fragment_offset > message_len` | Reject | `src/transport/xgl_fragment_process.c` |
| `data_len > message_len - fragment_offset` | Reject | `src/transport/xgl_fragment_process.c` |
| Different `message_len` for existing key | Reject | `src/transport/xgl_fragment_process.c` |
| All ranges cover `[0, message_len)` | Remove buffer and deliver complete payload | `src/transport/xgl_fragment_process.c`, `test/test_fragment.cpp` |

## Budget

Reassembly is bounded by two budgets:

- maximum concurrent reassembly buffers
- maximum message size
- optional aggregate in-flight reassembly bytes

Over-budget fragments are dropped and counted. RESET clears only the target
connection/session reassembly state through `xgl_fragment_clear_reassembly_scope()`.

| Budget | Source | Cleanup |
| --- | --- | --- |
| Concurrent buffers | `max_reassembly_buffers` | New buffer allocation fails when full |
| Message bytes | `max_message_size` | Oversized message is rejected |
| Aggregate bytes | `max_reassembly_bytes` | In-flight reservation prevents exhaustion |
| Timeout | `reassembly_timeout_ms` | `xgl_fragment_process_timeouts()` frees stale buffers |
| Scope reset | peer + `connection_id` + `session_epoch` | RESET/CLOSE clear only matching buffers |

## Attack Surface

Fragmentation is a primary memory-exhaustion entrypoint. Production configurations must limit maximum message length, concurrent reassemblies, per-peer budget, and global budget.

## Traceability

| Rule | Source | Tests |
| --- | --- | --- |
| Fragment value format | `src/wire/xgl_wire_ext.c` | `test/test_wire.cpp` |
| Send budget planning | `src/transport/xgl_transport_send_plan.c` | `test/test_transport.cpp` |
| Fragment emission | `src/transport/xgl_transport_send_fragment.c` | `test/test_transport.cpp` |
| Reassembly key and buffers | `src/transport/xgl_fragment_reassembly.c` | `test/test_fragment.cpp` |
| Range merge/overlap rules | `src/transport/xgl_fragment_range.c` | `test/test_fragment.cpp` |
| Timeout and scope cleanup | `src/transport/xgl_fragment_maintenance.c`, `src/transport/xgl_transport_peer.c` | `test/test_fragment.cpp`, `test/test_transport.cpp` |
