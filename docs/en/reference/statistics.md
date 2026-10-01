# Statistics

`xgl_statistics_t` contains separate datalink, network and transport counters, retransmissions, CRC errors and RTT metrics.

## Interpretation

A packet can appear in multiple layer counters. Do not add their byte counts to calculate application throughput. Submission success and transport TX counters do not prove remote application delivery; measure accepted payloads at the receiving callback.

## Access

Call `xgl_stats_get()` or `xgl_stats_reset()` while holding the application's exclusive access to the instance. There are no protocol mutexes or atomic snapshot promises. Counter reset does not reset peers, packet numbers, sessions or pending data.

## Build profiles

Full and Embedded enable `XGL_FEATURE_STATISTICS`. Boot removes the statistics object and counter updates. For a valid instance, `xgl_stats_get()` / `xgl_stats_reset()` return `XGL_ERR_UNSUPPORTED`; the getter leaves its output unchanged. Null-pointer and initialization checks still run first.

## RTT and memory

RTT observations include valid ACK samples for packets that were never retransmitted. Zero elapsed time and elapsed values outside the signed time range are excluded. The mean uses integer milliseconds. Before a sample, mean and maximum are zero and minimum is `UINT32_MAX`. Observations stop accumulating when the sample count reaches `UINT32_MAX`; protocol RTO estimation continues independently.

`memory_used` is the complete instance workspace reservation measured by `xgl_memory_requirements()`, including initialization storage and resource pools. It is not the number of active payload-pool bytes. Both static and allocator-backed initialization use a fixed workspace, so `memory_peak` equals this reservation. Reset preserves both memory values and clears RTT observations and layer counters without changing the RTT/RTO control state.
