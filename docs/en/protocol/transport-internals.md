# Transport Internals

Transport is the sole owner of reliable transitions. Its helpers are grouped by their state owner and borrowed/owned data contract, rather than by interchangeable generic algorithms.

## Peer State and Helpers

`xgl_transport_peer.c` performs exact lookup, bounded creation, terminal failure and explicit close. Each peer contains its TX window, wait-ACK queue, RTT estimator, next RX number and optional retained receive/message state. The context stores configured capacities, explicit current time, resource services and the peer list. There is no second global window or short-session identity.

Queue/index/window helpers operate on the selected peer's objects. They do not choose a fallback peer, reset scopes or submit raw PHY bytes. `xgl_transport_ack.c` validates all claims before changing records, window or RTT. Packet numbers at or beyond the next unassigned TX number are invalid ACK targets.

## Ownership Across Calls

| Input or retained state | Contract |
| --- | --- |
| Send request | Borrowed for the call; accepted reliable bytes are copied |
| Received packet/frame | Borrowed until the synchronous callback returns |
| Reliable record | Owns payload and fragment metadata until completion/termination |
| Out-of-order packet | Owns payload and TLVs before it may be SACKed |
| Complete RX message | Retained by peer while application returns BUSY |
| Pending TX message | Owns the unsent tail across window and local-capacity stalls |

Each allocation is released through the same resource service. Clearing a peer's data does not reset its identity, consumed DATA numbers or terminal failure state. Security replay and authentication counters are never owned by this cleanup.

## Scheduling and Observability

`xgl_transport_run` receives explicit time from the public step operation. The timeout collector combines wait-ACK timers, positive local-retry delays, reclaimable unused-peer idle timers and fragment expiry. No cached global earliest deadline can become stale when a peer queue changes.

Timeout retries and SACK-triggered retries use the same logical packet path. Temporary BUSY or memory pressure keeps accepted storage and schedules another attempt. Hard failure, retry exhaustion, acknowledged reassembly expiry and RESET terminate the scope observably. Error callbacks run synchronously and must not reenter the instance. A receiver retaining data during application BUSY needs another step after application capacity becomes available.
