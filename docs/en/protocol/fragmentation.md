# Fragmentation

Embedded and Full can enable bounded protocol fragmentation. Boot excludes it; firmware transfer blocks belong to the Boot application protocol. Profiles retain the same wire encoding, but a receiver without fragmentation support cannot accept fragmented messages.

## Fragment Identity and Bounds

FRAGMENT_EXT carries `message_id`, `offset` and `total_len` as 32-bit values. Reassembly uses the exact peer scope plus message ID. Total length must be nonzero, fit the explicit `max_message_size`, fit the configured byte budget and remain consistent across fragments. Offsets and lengths are validated before addition or copying. The manager keeps at most 16 received ranges per incomplete message.

A completely covered repeated range must contain identical bytes. A partially overlapping range is rejected: new bytes cannot be acknowledged unless retained. A message becomes complete only when coverage reaches its entire declared length.

## Reliable Message Pump

A reliable submission that requires fragmentation owns one bounded copy of the complete application message per peer. Fragments enter that peer's reliable queue only as its window and the global TX-record budget permit. ACK processing and subsequent steps continue the unsent tail, including a one-packet window. Every fragment has its own reliable DATA number; retransmission retains that number and fragment metadata.

The complete-message copy is released once all fragments have entered owned reliable records. Local BUSY or temporary memory pressure retains an accepted message and schedules a bounded retry. A hard error or retry exhaustion terminates the scope, releases its owned data and reports failure. Successful submission means acceptance, not completion at the remote application.

## Receive Ownership and Failure

Incomplete reassembly slots, all retained reassembly bytes and pending complete messages are bounded explicitly. After the last fragment, application BUSY keeps the complete message under the peer's ownership; later `xgl_step` calls retry delivery. Application capacity recovery must drive another step. An ACKed incomplete reliable message cannot expire silently: expiry fails its scope before storage is released.

Unreliable fragmentation submits fragments synchronously without the reliable whole-message retry contract. A later submission error can leave earlier fragments already transmitted. Applications requiring recovery should use reliable messages or an application-level block protocol.

Verification: `test/test_fragment.cpp` and `test/test_transport.cpp`.
