# Fragmentation

Embedded and Full can enable bounded protocol fragmentation. Boot excludes it; firmware transfer blocks belong to the Boot application protocol. Profiles retain the same wire encoding, but a receiver without fragmentation support cannot accept fragmented messages.

## Fragment Identity and Bounds

FRAGMENT_EXT carries `message_id`, `offset` and `total_len` as 32-bit values. Reassembly uses the exact peer scope plus message ID. Total length must be nonzero, fit the explicit `max_message_size`, fit the configured byte budget and remain consistent across fragments. Offsets and lengths are validated before addition or copying. The manager keeps at most 16 received ranges per incomplete message.

A fragmented DATA frame requires exactly one FRAGMENT extension. Receive admission scans the complete TLV stream and rejects duplicate, missing or malformed fragment metadata before caching an out-of-order packet or acknowledging it. Valid unknown extensions can appear before or after FRAGMENT. Message capacity and reassembly ownership are still checked when bytes enter reassembly.

A completely covered repeated range must contain identical bytes. A partially overlapping range is rejected: new bytes cannot be acknowledged unless retained. A message becomes complete only when coverage reaches its entire declared length.

## Reliable Message Pump

A reliable submission that requires fragmentation owns one bounded copy of the complete application message per peer. Fragments enter that peer's reliable queue only as its window and the global TX-record budget permit. ACK processing releases records; transport maintenance in the same or a later step continues the unsent tail, including a one-packet window. ACK reception itself does not synchronously pump DATA. Every fragment has its own reliable DATA number; retransmission retains that number and fragment metadata. Admission rejects a message whose complete fragment count exceeds the remaining usable DATA numbers.

The complete-message copy is released once all fragments have entered owned reliable records. Local BUSY or temporary memory pressure retains an accepted message and schedules a bounded retry. A hard error or retry exhaustion terminates the scope, releases its owned data and reports failure. Successful submission means acceptance, not completion at the remote application.

## Receive Ownership and Failure

Incomplete reassembly slots, all retained reassembly bytes and pending complete messages are bounded explicitly. After the last fragment, application BUSY keeps the complete message under the peer's ownership; later `xgl_step` calls retry delivery. Application capacity recovery must drive another step. An ACKed incomplete reliable message cannot expire silently: expiry fails its scope before storage is released.

With advanced runtime enabled, the absolute reassembly deadline also covers SACKed out-of-order fragments and starts at the earliest admitted fragment of that message. Entering ordered reassembly does not restart it, and a zero TX quota does not stop expiry cleanup. This message deadline does not independently expire ordinary unfragmented out-of-order packets. See [advanced runtime options](../../../guides/advanced-runtime.md).

During maintenance, BUSY and NO_MEMORY preserve the pending complete message and its byte reservation without returning a terminal error. A later hard application error releases the owned message, fails the exact scope and is returned by maintenance; the failure callback occurs once. Already-accepted fragments remain part of that scope's numbering history. Failed ACK submission never rolls this ownership or history back.

Unreliable fragmentation submits fragments synchronously without the reliable whole-message retry contract. A later submission error can leave earlier fragments already transmitted. Applications requiring recovery should use reliable messages or an application-level block protocol.

Verification: `test/unit/transport/test_fragment.cpp`, `test/unit/transport/test_transport.cpp` and `test/unit/transport/test_transport_rx_contracts.cpp`.
