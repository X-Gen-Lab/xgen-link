# Extensions

Each TLV is `type:u8 | value_length:u8 | value`. TLVs are bounded by `header_len`, not by payload contents. Unknown types are skipped. Duplicate DATA_TYPE, SESSION or SECURITY singleton extensions are rejected; transport accepts exactly one ACK_RANGE or SACK extension per ACK packet.

## Defined Values

| Type | Value bytes | Meaning |
| --- | --- | --- |
| SESSION = 1 | 12 | `epoch:u32`, `incarnation_id:u64` |
| ACK_RANGE = 2 | `9 + 4*n` | `largest_ack:u32`, `ack_delay_us:u32`, `count:u8`, then `gap:u16,length:u16` |
| SACK = 3 | `5 + n` | `base:u32`, `bitmap_length:u8`, bitmap bytes |
| FRAGMENT = 4 | 12 | `message_id:u32`, `offset:u32`, `total_length:u32` |
| SECURITY = 5 | 13 | `key_id:u32`, `security_seq:u64`, `tag_length:u8` |
| DATA_TYPE = 8 | 1 | Application type or CONTROL operation |

Values use little-endian integers. ROUTE and TIMESTAMP identifiers are reserved. SESSION adds 14 frame bytes; FRAGMENT adds 14; SECURITY adds 15. Missing SESSION denotes epoch zero. `incarnation_id` is carried metadata; its presence does not install trust or replace the peer key.

## ACK Semantics

The first ACK range has gap zero and begins at `largest_ack`; length is a positive packet count. Later ranges skip their gap plus the interval boundary before describing lower numbers. Invalid ranges, underflow and future numbers reject the entire ACK before queue, window or RTT mutation.

SACK bit `i` describes `base + i`. Numbers below base are cumulatively acknowledged. Set bits mean the receiver owns the corresponding bytes; missing bits can drive retransmission. The current sender emits at most eight bitmap bytes. Boot can consume ACK forms supported by the transport even when out-of-order buffering is compiled out.

## Composition

Network composes or validates DATA_TYPE and nonzero SESSION epoch extensions. Retransmission retains the fragment metadata but recomposes the frame. Authentication fills SECURITY from the installed directional association for each attempt; callers must not treat a retained DATA packet as a retained signed frame.

Verification: `test/unit/wire/test_wire.cpp`.
