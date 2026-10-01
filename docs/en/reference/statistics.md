# Statistics

`xgl_statistics_t` contains separate datalink, network and transport counters, retransmissions, CRC errors and RTT metrics.

## Interpretation

A packet can appear in multiple layer counters. Do not add their byte counts to calculate application throughput. Submission success and transport TX counters do not prove remote application delivery; measure accepted payloads at the receiving callback.

Network and datalink TX packet/byte counters commit only after successful lower-layer submission for copied, in-place and forwarded frames. Network bytes describe payload bytes; datalink bytes include framing. Failures do not count as transmitted packets. The retransmission counter increments at each successful retry submission, even when a later retry in the same SACK batch fails. Repeated SACKs that do not trigger another submission do not increment it.

The streaming parser and raw-frame ingress share CRC classification: header and complete-frame CRC failures increment their respective counters and synchronously report `XGL_ERR_CRC_FAILED`. Classification uses the current validation result without computing CRC again for statistics.

Stream recovery records the original rejected candidate once; false magic found while scanning buffered bytes does not add diagnostics. Each recovered complete frame contributes its own packet and complete-frame byte counts. Upper-layer rejection of a wire-valid frame does not rescan its payload.

Datalink receive statistics distinguish frame validation from driver calls. Wire format and CRC rejection increment `rx_errors`; CRC also reports its category. Ordinary wire format rejection does not call the error callback, while exceeding the datalink frame-size policy reports `XGL_ERR_INVALID_FRAME`. Partial-frame expiry resets and reports once. Errors returned by the PHY `rx` callback propagate to the step caller without counting as frame errors or triggering a second protocol error callback; driver fault counters belong to the adapter. A valid complete frame counts as received before upper-layer admission, so BUSY does not reverse that count. TX PHY failures increment datalink `tx_errors` and report the failure. These directional meanings remain explicit; adding errors across layers does not yield a fault count.

## Send Admission

For ordinary sends and both zero-copy APIs, a PHY failure contributes one TX error to each layer reached by that submission. This includes a driver returning `XGL_ERR_BUSY`, `XGL_ERR_TX_FAILED` or `XGL_ERR_INVALID_PARAM`. No layer counts that failed submission as a transmitted packet or byte.

For zero-copy sending, an attempt starts after basic descriptor validation and before entering the shared network send path. Null pointers, zero payload length, invalid priority, unsupported reliable sending and basic buffer bounds fail before that point and do not increment TX errors. Once admitted, a route, MTU, exact headroom or lower-layer failure increments transport `tx_errors`; lower layers retain their own validation and submission counters. The error code alone cannot distinguish an invalid caller descriptor from a driver's error. A layout query never counts as an attempt, whether it succeeds or fails. See [Zero-Copy](../guide/zero-copy.md).

Admission here means that logical sending has begun. It does not mean that the PHY accepted the frame, that a reliable message was queued, or that the peer received it.

## Network Counters

The following table describes network-layer counters. Unlisted counters do not change for that event; datalink and transport may independently count their own processing.

| Event | Network counter updates |
| --- | --- |
| Local frame passes address and security policy | `rx_packets += 1`, `rx_bytes += payload_len`, before calling transport |
| Transport rejects an admitted local frame, including BUSY | No rollback of network RX counts and no additional network error |
| Originated or forwarded frame is accepted by the lower layer | `tx_packets += 1`, `tx_bytes += payload_len` |
| Forwarded frame is accepted | No network RX packet/byte increment; those RX counters describe local admission |
| Address or local security policy rejects a frame | `rx_dropped += 1` |
| Forwarding disabled, no forwarding route, expired TTL or exceeded egress MTU | `rx_dropped += 1` |
| Forwarding scratch allocation fails | `rx_dropped += 1`, `tx_errors += 1` |
| Forwarding reaches the lower layer and fails | `tx_errors += 1`; no transmitted packet/byte or RX-drop increment |
| Originated logical send fails during routing, framing, budget checking or lower submission | `tx_errors += 1` |
| Internal complete-frame entry rejects wire encoding or the instance frame limit | `rx_errors += 1`; no `rx_dropped` increment |

Public streaming ingress normally rejects invalid wire frames in datalink before they reach network. The last row describes the independent internal complete-frame entry; it does not add a second network error to every datalink rejection. Forwarders validate the wire frame but do not authenticate transit payloads locally; the final endpoint applies its own security policy.

## Access

Call `xgl_stats_get()` or `xgl_stats_reset()` while holding the application's exclusive access to the instance. There are no protocol mutexes or atomic snapshot promises. Counter reset does not reset peers, packet numbers, sessions or pending data.

## Build profiles

Full, Embedded and Embedded-lite enable `XGL_FEATURE_STATISTICS`. Boot removes the statistics object and counter updates. For a valid instance, `xgl_stats_get()` / `xgl_stats_reset()` return `XGL_ERR_UNSUPPORTED`; the getter leaves its output unchanged. Null-pointer and initialization checks still run first.

## RTT and memory

RTT observations include valid ACK samples for packets that were never retransmitted. Zero elapsed time and elapsed values outside the signed time range are excluded. The mean uses integer milliseconds. Before a sample, mean and maximum are zero and minimum is `UINT32_MAX`. Observations stop accumulating when the sample count reaches `UINT32_MAX`; protocol RTO estimation continues independently.

`memory_used` is the complete instance workspace reservation measured by `xgl_memory_requirements()`, including initialization storage and resource pools. It is not the number of active payload-pool bytes. Both static and allocator-backed initialization use a fixed workspace, so `memory_peak` equals this reservation. Reset preserves both memory values and clears RTT observations and layer counters without changing the RTT/RTO control state.
