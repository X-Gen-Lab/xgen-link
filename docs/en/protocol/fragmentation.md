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

## Receiver Side

The receiver validates the frame first, then finds a buffer by reassembly key. Only after ranges cover `[0, message_len)` is the assembled payload delivered to transport.

## Budget

Reassembly is bounded by two budgets:

- per-peer budget
- global budget

Over-budget fragments are dropped and counted. RESET clears only the target connection/session reassembly state.

## Attack Surface

Fragmentation is a primary memory-exhaustion entrypoint. Production configurations must limit maximum message length, concurrent reassemblies, per-peer budget, and global budget.
