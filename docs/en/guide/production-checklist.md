# Production Checklist

Use this page as an engineering review checklist before deploying XGL on an MCU or multi-node system. Every item should have evidence in code, configuration, tests, or runtime logs.

## Configuration

- `source_id` is non-zero and not in a reserved address range.
- Route table covers all target nodes, and `max_frame_size` matches PHY MTU.
- `auth_required=true` with a valid `auth_provider`.
- `auth_key_id` matches the device key-management system.
- `enable_encryption` and `enable_compression` remain disabled until codec/security models are fully wired.
- Production/no-heap profiles have explicit allocator behavior and do not silently fall back to malloc.

## Security

- Auth provider `tag_len` is fixed and satisfies `0 < tag_len <= XGL_AUTH_TAG_MAX_LEN`.
- `sign` and `verify` use the same AAD boundary for header/extensions/payload.
- Key id and nonce/material id generation and rotation are handled by the application or secure module.
- Replay window capacity is sufficient for the maximum reorder window.
- Multi-hop forwarding verifies CRC/auth behavior after TTL mutation.
- Bad auth, replay, wrong session, and wrong connection packets are not ACKed.

## Reliability

- Reliable window size matches link RTT, bandwidth, and RAM budget.
- Retry limit matches application latency tolerance.
- ACK range/SACK passes loss/reorder/duplicate injection.
- RESET/CLOSE clears only the target peer/connection/session.
- Application callback does not block the protocol loop.

## Memory

- Peer state, reliable queue, RX buffer, and fragment reassembly upper bounds are calculable.
- Fragment global budget and per-peer budget are configured.
- Worst-case payload, fragment count, and route MTU have a capacity analysis.
- No-heap smoke passes.
- Footprint report fits target MCU RAM/Flash.

## Real-Time and Power

- ISR only enqueues PHY RX data and does not call parser/auth/transport.
- Main loop or RTOS task calls `xgl_run()`.
- `xgl_next_deadline_ms()` is used to compute sleep time.
- Time provider is monotonic and handles wraparound.
- PHY send/receive does not block for long while protocol locks are held.

## Diagnostics

- Stats distinguish CRC, auth, replay, route, MTU, timeout, and fragment budget failures.
- Release builds retain required error callbacks.
- Field logs do not print keys, raw tags, or sensitive payloads.
- Long soak tests cover multi-node forwarding, retransmission, and fragmentation.

## Release

- `ctest --preset gcc-test --output-on-failure` passes.
- `xgl_release_validation` passes.
- `xgl_docs` passes.
- cppcheck is installed and passes.
- SDK consumer smoke passes.
- Worktree has no unexplained source changes.
