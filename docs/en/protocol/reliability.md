# Reliability

One peer owns one TX window, one reliable queue and one RX ordering state, plus an RTT estimator when adaptive RTO is enabled. The sole key is `(remote_id, connection_id, session_epoch)`. There is no instance-global fallback window or separate PHY retransmission engine.

## Transmit Lifecycle

Reliable DATA is copied before submission. Failed admission does not consume its packet number. A successful first lower-layer send marks the retained packet sent, including at timestamp zero, then advances the DATA number. ACK completion frees the queue record and advances only the owning window.

Before sending the first HELLO, transport checks frame and message length, scope state, number space, window and global TX capacity, and reserves the reliable record and payload. Reliable fragmentation also reserves the complete-message copy and the first fragment's record, payload and extensions. A local admission failure sends no HELLO. A successful HELLO does not guarantee DATA has reached the receiver, so unused receive reservations still need a bounded lifetime.

The initial ACK timeout uses the request's nonzero `timeout_ms` override first. Without an override, a peer with an accepted RTT sample uses its estimated RTO; a peer without samples uses the configured `protocol.ack_timeout_ms`. RTT arithmetic uses a wide intermediate before clamping, including the largest accepted 32-bit millisecond sample.

With the compiled option `XGL_ENABLE_ADAPTIVE_RTO=OFF`, peers retain no RTT estimator and ACKs collect no RTT samples. Without a request override, the configured ACK timeout remains effective. Request `timeout_ms` overrides, retry limits and exponential backoff still apply. This changes timeout selection without changing ACK/SACK or the wire format. See [build and test](../getting-started/build-and-test.md#compiled-resource-options) for compiled resources and SDK identities.

Timeout and SACK retries use the same transport routine and pass through network and datalink again. DATA number and payload remain stable; authentication obtains a fresh sequence. Temporary BUSY or NO_MEMORY retains accepted bytes and schedules a positive delay of the configured default timeout clamped to 1–100 ms. Successful retries update timestamp, retry count and exponential backoff, capped at 30000 ms. A hard lower-layer failure or exhausted retry count fails the entire peer and reports once.

ACK handling validates feedback and releases acknowledged records. SACK only marks eligible gaps for fast retransmission; it does not transmit DATA recursively from the receive path. Transport maintenance in the same or a later `xgl_step()` performs retries and advances an accepted fragmented message after ACKs release its window. Ready work participates in `xgl_next_timeout()`; a retained local-capacity retry keeps its positive delay.

SACK can request fast retransmission only before an outstanding packet has been successfully retransmitted. Once any retry succeeds, later recovery for that record uses its RTO. Duplicate feedback therefore cannot consume the remaining retry budget; newly reported gaps can still schedule other original transmissions. BUSY does not consume a retry. Each successful retry updates the aggregate counter at the send commit point, even when a later gap encounters failure or backpressure.

## Receive and Acknowledgement

In-order DATA is acknowledged only after application acceptance or retained transport ownership. An application BUSY response leaves an unretained in-order packet unacknowledged. Out-of-order packets are SACKed only after a successful copy; once retained, they survive temporary application backpressure. Duplicates are not delivered again. RTT samples exclude retransmitted packets.

Local transport admits only DATA, ACK and CONTROL packet types. Reliable broadcast is rejected before peer admission; it cannot consume a unicast scope's receive numbers. Reliable DATA number `UINT32_MAX` is reserved and rejected before caching or acknowledgement. An exhausted scope never wraps its receive history to zero.

Fragment metadata is validated before out-of-order caching or SACK: exactly one well-formed FRAGMENT extension is required, with valid payload bounds and a supported total length. Within one scope, an active `message_id` has immutable reliability, application type and total length. Checks cover both incomplete reassembly and owned out-of-order fragments, rejecting conflicts before ACK/SACK without modifying existing bytes. Structurally valid unknown TLVs remain compatible.

An ACK or SACK transmission failure is returned without undoing accepted payloads, receive numbers or retained bytes. Reassembly reliability records actual admission; failed ACK submission cannot revoke it. Contiguous owned packets are still processed, retaining the first error. Retransmitted accepted DATA can receive another ACK without duplicate application delivery.

A rejected first packet does not commit reliable RX history. If processing created a new peer but admitted no data and did not fail the scope, its reservation is returned immediately, including when idle reclamation is disabled. Existing peers and scopes with accepted, retained or failed state are preserved. This rollback must never erase acknowledged history or authentication replay state.

ACKs match the exact scope, validate the complete TLV stream and acknowledgement claims before mutation, and cannot complete unsent or future packets. Repeated valid ACKs are idempotent. Header packet number is never an ACK target.

## Failure and Recovery

HELLO ensures a peer without resetting existing state. An empty, unused peer reserved by HELLO has an independent 5000 ms lease starting at its first reception. Duplicate HELLO does not renew it. The lease participates in `xgl_next_timeout()`; transport maintenance with caller-supplied explicit time reclaims an expired empty reservation even when `peer_idle_timeout_ms=0`. An earlier ordinary idle deadline can reclaim it sooner. Reliable numbering history, failure state or owned data prevents HELLO lease reclamation.

RESET only targets an existing exact scope; its first effect is terminal cancellation, with repeated requests producing no extra transition or error callback. Unknown RESET does not allocate a peer.

Reassembly expiration follows each message's own admission attributes. A purely unreliable message expiration releases that message and returns TIMEOUT without failing a scope that previously received reliable DATA or removing unrelated reliable TX. Expiration of a message owning admitted reliable bytes fails its scope and reports once, including when its ACK submission failed. Expired records are detached from the active queue before notifying scope owners and releasing storage, so scope cleanup cannot invalidate the expiration traversal.

Failed reliable scopes cannot accept new reliable work. `xgl_close_peer` releases an unauthenticated scope and reports CANCELLED once if accepted work remains. With authentication, close the security session, which also retires transport ownership while retaining the nonce-domain tombstone. Establish a new epoch before reconnecting. Without authentication, the caller must drain old link traffic; epoch selection alone is not a security mechanism.

Transport maintenance prioritizes its first hard error while continuing other scopes; without a hard error, it returns the first immediate feedback failure. BUSY or NO_MEMORY encountered while retrying already-owned work retains that work and is normal maintenance backpressure. This does not hide an immediate receive admission or ACK submission error. ACK BUSY/NO_MEMORY after a buffered packet has been delivered is not retained-data backpressure and is returned by maintenance. A pending complete message whose application callback later returns a hard error fails its scope, releases owned bytes and reports once.

Once a scope has used TX or RX reliable numbering, idle time never reclaims its history. Only unused, empty peers may be reclaimed automatically; used scopes require explicit close.

Verification: `test/unit/transport/test_reliable.cpp`, `test/unit/transport/test_reliable_owned.cpp`, `test/unit/transport/test_transport.cpp`, `test/unit/transport/test_transport_rx_contracts.cpp`, `test/unit/transport/test_transport_fragment_scope.cpp`, `test/unit/transport/test_transport_runtime_contracts.cpp`, `test/unit/transport/test_transport_fixed_rto.cpp` and `test/integration/test_transport_admission.cpp`.
