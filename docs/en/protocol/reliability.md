# Reliability

One peer owns one TX window, one reliable queue, one RTT estimator and one RX ordering state. The sole key is `(remote_id, connection_id, session_epoch)`. There is no instance-global fallback window or separate PHY retransmission engine.

## Transmit Lifecycle

Reliable DATA is copied before submission. Failed admission does not consume its packet number. A successful first lower-layer send marks the retained packet sent, including at timestamp zero, then advances the DATA number. ACK completion frees the queue record and advances only the owning window.

Timeout and SACK retries use the same transport routine and pass through network and datalink again. DATA number and payload remain stable; authentication obtains a fresh sequence. Temporary BUSY or NO_MEMORY retains accepted bytes and schedules a positive delay of the configured default timeout clamped to 1–100 ms. Successful retries update timestamp, retry count and exponential backoff, capped at 30000 ms. A hard lower-layer failure or exhausted retry count fails the entire peer and reports once.

## Receive and Acknowledgement

In-order DATA is acknowledged only after application acceptance or retained transport ownership. An application BUSY response leaves an unretained in-order packet unacknowledged. Out-of-order packets are SACKed only after a successful copy; once retained, they survive temporary application backpressure. Duplicates are not delivered again. RTT samples exclude retransmitted packets.

ACKs match the exact scope, validate the complete TLV stream and acknowledgement claims before mutation, and cannot complete unsent or future packets. Repeated valid ACKs are idempotent. Header packet number is never an ACK target.

## Failure and Recovery

HELLO ensures a peer without resetting existing state. RESET only targets an existing exact scope; its first effect is terminal cancellation, with repeated requests producing no extra transition or error callback. Unknown RESET does not allocate a peer.

Failed reliable scopes cannot accept new reliable work. `xgl_close_peer` releases an unauthenticated scope and reports CANCELLED once if accepted work remains. With authentication, close the security session, which also retires transport ownership while retaining the nonce-domain tombstone. Establish a new epoch before reconnecting. Without authentication, the caller must drain old link traffic; epoch selection alone is not a security mechanism.

Once a scope has used TX or RX reliable numbering, idle time never reclaims its history. Only unused, empty peers may be reclaimed automatically; used scopes require explicit close.

Verification: `test/test_reliable.cpp` and `test/test_transport.cpp`.
