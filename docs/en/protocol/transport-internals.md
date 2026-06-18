# Transport Internals

The transport layer is the most complex layer in the XGL protocol stack, comprising 28 source files. This document details each sub-module's responsibilities, data structures, and interactions.

## Module Overview

The transport layer is divided into 6 sub-modules by function:

```text
src/transport/
├── Send Path (5 files)
│   ├── transport.c                 — init/destroy
│   ├── transport_send.c            — send entry point
│   ├── transport_send_plan.c       — single-frame vs fragmentation decision
│   ├── transport_send_packet.c     — single-packet send
│   └── transport_send_fragment.c   — fragmented send
├── Receive Path (5 files)
│   ├── transport_receive.c         — receive entry point
│   ├── transport_receive_ack.c     — ACK receive processing
│   ├── transport_receive_peer.c    — peer scope resolution
│   ├── transport_receive_order.c   — ordered receive
│   └── transport_delivery.c        — deliver to application callback
├── Reliability (4 files)
│   ├── reliable.c                  — reliable queue management
│   ├── reliable_packet.c           — packet object management
│   ├── reliable_ack.c              — ACK range processing
│   └── reliable_timeout.c          — timeout processing
├── ACK/SACK (4 files)
│   ├── transport_ack.c             — ACK generation
│   ├── transport_ack_send.c        — ACK transmission
│   ├── transport_sack.c            — SACK generation
│   └── transport_sack_send.c       — SACK transmission
├── Fragmentation (5 files)
│   ├── fragment.c                  — fragmentation core
│   ├── fragment_range.c            — range merging
│   ├── fragment_reassembly.c       — reassembly buffer management
│   ├── fragment_maintenance.c      — timeout maintenance
│   └── fragment_process.c          — fragment processing entry
└── Control/Runtime (5 files)
    ├── transport_control.c         — HELLO/RESET handling
    ├── transport_retransmit.c      — retransmission logic
    ├── transport_runtime.c         — runtime scheduling
    ├── transport_interface.c       — layer interface adaptation
    └── transport_memory.c          — memory allocation strategy
```

## Peer State Design

Each remote node maintains an independent transport peer state:

### Peer Triplet

Peer state is uniquely identified by `(peer_id, connection_id, session_epoch)`:

- **peer_id**: TX path uses `target_id`; RX/ACK path uses received packet's `source_id`.
- **connection_id**: Production connection context ID.
- **session_epoch**: Production session epoch.

This triplet isolates: reliable queue, sliding window, RTT estimator, RX buffer, and replay/reassembly cleanup scope.

### Peer Lifecycle

1. **Created**: First time seeing a new peer.
2. **HelloSent**: HELLO sent for this session.
3. **Established**: HELLO ACK received; peer session known.
4. **Active**: Normal data transfer.
5. **Timeout**: `last_active_ms` exceeded; resources cleaned up.

## Send Path

### Call Chain

```text
xgl_send()
  → xgl_transport_send()
    → transport_send_plan.c (single-frame vs fragmentation decision)
      → single: transport_send_packet.c
      → fragmented: transport_send_fragment.c
        → reliable_queue enqueue
        → xgl_layer_send() → network → datalink → PHY
```

### Send Decision

1. Check sliding window has free slots.
2. Check if payload exceeds route MTU.
   - Within MTU: single-packet send.
   - Exceeds MTU + fragmentation enabled: fragmented send.
   - Exceeds MTU + fragmentation disabled: return `XGL_ERR_BUFFER_TOO_SMALL`.
3. Reliable: enqueue to `reliable_queue`, retain data copy.
4. Unreliable: pass directly to network layer, no queuing.

### Transactional Reliable Send

Reliable sending follows strict transactional semantics:

1. Enqueue to reliable queue first.
2. Pass to network layer second.
3. Enqueue failure → do not send.
4. Network send failure → remove queue record.
5. Packet number and window state only committed after network accepts the send.

## Receive Path

### Receive State Table

| Condition | Behavior |
| --- | --- |
| `packet_number == rx_next_packet_number` | Deliver and advance contiguous window |
| `packet_number > rx_next_packet_number` | Buffer as out-of-order, send ACK/SACK |
| `packet_number < rx_next_packet_number` | Treat as duplicate, do not re-deliver |
| connection/session mismatch | Reject, do not pollute other peer state |

### Ordered Delivery

Out-of-order packets are buffered by packet number. Only contiguous payload starting from `rx_next_packet_number` is delivered to the application callback. Out-of-order packets are cached in the `rx_buffered` linked list.

## Reliable Queue

### 32-Bucket Hash Index

The reliable queue uses a 32-bucket hash table for accelerated packet lookup:

- Lookup: `packet_number % 32` locates the bucket; linear scan within the bucket.
- Enables fast packet location during ACK/SACK processing.
- Efficient with small window sizes due to short bucket chains.

### Timeout Retransmission

1. `xgl_reliable_process_timeouts()` traverses the wait-ack list.
2. Checks `current_time_ms - send_timestamp >= timeout_ms` for each packet.
3. Timed-out packets: retransmit + `retry_count++` + exponential backoff.
4. `retry_count > max_retry_count`: mark as retry exhausted, report via error_callback.

### Exponential Backoff

```text
backoff = initial_timeout_ms × 2^retry_count
```

Clamped to `[XGL_MIN_RTO_MS (100ms), XGL_MAX_RTO_MS (5000ms)]`.

## ACK/SACK Generation

- **ACK**: Sent after receiving reliable data packets; carries `largest_ack` and optional ACK ranges.
- **SACK**: Sent when out-of-order packets (gaps) are detected; describes received packets to trigger fast retransmission.
- Both retain the original `connection_id`, `session_epoch`, and `session_id` from the received reliable packet.

## Fragmentation

### Reassembly Buffer

```text
xgl_reassembly_buffer_t
├── received_ranges[16]    (range tracking array)
├── data                   (reassembly data buffer)
├── data_len               (total message length)
├── first_fragment_time    (timeout tracking)
└── timeout_ms             (reassembly timeout)
```

### Range Merging Strategy

Uses 16 ranges (not per-byte bitmap) to track received byte ranges:

1. New fragment arrives: find overlapping or adjacent ranges.
2. Merge: extend existing range or create new range.
3. If `received_ranges` overflows 16: reject new fragment.
4. When `received_bytes == data_len`: reassembly complete.

### Timeout Cleanup

- Default: `XGL_FRAGMENT_TIMEOUT_MS = 5000` ms
- RESET/CLOSE clears all reassembly buffers for the corresponding `(source_id, connection_id, session_epoch)` scope.

## Control Messages

| Control Type | DATA_TYPE Value | Purpose |
| --- | --- | --- |
| HELLO | `0x0E` | Establish peer session |
| RESET | `0x0F` | Reset specific peer/connection |

Control messages only clean up the corresponding peer/connection/session, not global ACK, fragment, or replay state.

## Runtime Scheduling

`xgl_transport_run()` is called periodically by `xgl_run()` (recommended every 10-100ms):

1. Traverse all peer states.
2. Process timeout retransmissions.
3. Process fragment reassembly timeouts.
4. Clean up inactive peer states.

## Evidence

| Module | Source | Test |
| --- | --- | --- |
| Transport init/send/receive | `src/transport/transport.c`, `transport_send.c`, `transport_receive.c` | `test/test_transport.cpp` |
| Reliable queue + timeout | `src/transport/reliable.c`, `reliable_timeout.c` | `test/test_reliable.cpp` |
| ACK/SACK | `src/transport/transport_ack.c`, `transport_sack.c` | `test/test_transport.cpp` |
| Fragment/reassembly | `src/transport/fragment.c`, `fragment_reassembly.c` | `test/test_fragment.cpp` |
| Sliding window | `src/transport/xgl_window.c` | `test/test_window.cpp` |
| RTT estimator | `src/transport/xgl_rtt.c` | `test/test_rtt.cpp` |
| Peer state | `src/transport/transport_peer.c` | `test/test_transport.cpp` |