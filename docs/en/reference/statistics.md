# Statistics

`xgl_statistics_t` contains separate datalink, network and transport counters, retransmissions, CRC errors and RTT metrics.

## Interpretation

A packet can appear in multiple layer counters. Do not add their byte counts to calculate application throughput. Submission success and transport TX counters do not prove remote application delivery; measure accepted payloads at the receiving callback.

Network and datalink TX packet/byte counters commit only after successful lower-layer submission for copied, in-place and forwarded frames. Network bytes describe payload bytes; datalink bytes include framing. Failures do not count as transmitted packets. The retransmission counter increments at each successful retry submission, even when a later retry in the same SACK batch fails. Repeated SACKs that do not trigger another submission do not increment it.

The streaming parser and raw-frame ingress share CRC classification: header and complete-frame CRC failures increment their respective counters and synchronously report `XGL_ERR_CRC_FAILED`. Classification uses the current validation result without computing CRC again for statistics.

Stream recovery records the original rejected candidate once; false magic found while scanning buffered bytes does not add diagnostics. Each recovered complete frame contributes its own packet and complete-frame byte counts. Upper-layer rejection of a wire-valid frame does not rescan its payload.

Datalink receive statistics distinguish frame validation from driver calls. Wire format and CRC rejection increment `rx_errors`; CRC also reports its category. Ordinary wire format rejection does not call the error callback, while exceeding the datalink frame-size policy reports `XGL_ERR_INVALID_FRAME`. Partial-frame expiry resets and reports once. Errors returned by the PHY `rx` callback propagate to the step caller without counting as frame errors or triggering a second protocol error callback; driver fault counters belong to the adapter. A valid complete frame counts as received before upper-layer admission, so BUSY does not reverse that count. TX PHY failures increment datalink `tx_errors` and report the failure. These directional meanings remain explicit; adding errors across layers does not yield a fault count.

## Access

Call `xgl_stats_get()` or `xgl_stats_reset()` while holding the application's exclusive access to the instance. There are no protocol mutexes or atomic snapshot promises. Counter reset does not reset peers, packet numbers, sessions or pending data.

## Build profiles

Full, Embedded and Embedded-lite enable `XGL_FEATURE_STATISTICS`. Boot removes the statistics object and counter updates. For a valid instance, `xgl_stats_get()` / `xgl_stats_reset()` return `XGL_ERR_UNSUPPORTED`; the getter leaves its output unchanged. Null-pointer and initialization checks still run first.

## RTT and memory

RTT observations include valid ACK samples for packets that were never retransmitted. Zero elapsed time and elapsed values outside the signed time range are excluded. The mean uses integer milliseconds. Before a sample, mean and maximum are zero and minimum is `UINT32_MAX`. Observations stop accumulating when the sample count reaches `UINT32_MAX`; protocol RTO estimation continues independently.

`memory_used` is the complete instance workspace reservation measured by `xgl_memory_requirements()`, including initialization storage and resource pools. It is not the number of active payload-pool bytes. Both static and allocator-backed initialization use a fixed workspace, so `memory_peak` equals this reservation. Reset preserves both memory values and clears RTT observations and layer counters without changing the RTT/RTO control state.
