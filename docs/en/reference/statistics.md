# Statistics

`xgl_statistics_t` contains separate datalink, network and transport counters, retransmissions, CRC errors and RTT metrics.

## Interpretation

A packet can appear in multiple layer counters. Do not add their byte counts to calculate application throughput. Submission success and transport TX counters do not prove remote application delivery; measure accepted payloads at the receiving callback.

## Access

Call `xgl_stats_get()` or `xgl_stats_reset()` while holding the application's exclusive access to the instance. There are no protocol mutexes or atomic snapshot promises. Counter reset does not reset peers, packet numbers, sessions or pending data.
