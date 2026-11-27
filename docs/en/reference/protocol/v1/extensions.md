# Extensions

Each TLV is `type:u8 | value_length:u8 | value`. TLVs are bounded by `header_len`, not by payload contents. Type `0` is invalid. Structurally valid unknown noncritical types are skipped. Duplicate DATA_TYPE, SESSION or SECURITY singleton extensions are rejected; transport accepts exactly one ACK_RANGE or SACK extension per ACK packet.

## Defined Values

| Type | Value bytes | Meaning |
| --- | --- | --- |
| SESSION = 1 | 12 | `epoch:u32`, `incarnation_id:u64` |
| ACK_RANGE = 2 | `9 + 4*n` | `largest_ack:u32`, `ack_delay_us:u32`, `count:u8`, then `gap:u16,length:u16` |
| SACK = 3 | `5 + n` | `base:u32`, `bitmap_length:u8`, bitmap bytes |
| SECURITY = 4 | 13 | `key_id:u32`, `security_seq:u64`, `tag_length:u8` |
| DATA_TYPE = 5 | 1 | Application type or CONTROL operation |

Values use little-endian integers. SESSION adds 14 frame bytes. SECURITY adds 15. Missing SESSION denotes epoch zero. `incarnation_id` is carried metadata; its presence does not install trust or replace the peer key.

## ACK Semantics

The first ACK range has gap zero and begins at `largest_ack`; length is a positive packet count. Later ranges skip their gap plus the interval boundary before describing lower numbers. Invalid ranges, underflow and future numbers reject the entire ACK before queue, window or RTT mutation.

SACK bit `i` describes `base + i`. A set bit confirms that the receiver owns those bytes. Records below `base` await explicit acknowledgement. Gaps within the reported range can trigger fast retransmission. An all-zero bitmap only hints at fast retransmission of `base`. The current sender emits at most eight bitmap bytes. Boot can consume ACK forms supported by the transport even when out-of-order buffering is compiled out.

## Composition

Network composes or validates DATA_TYPE and nonzero SESSION epoch extensions. Retransmission keeps the DATA packet number and payload and recomposes the frame. Authentication fills SECURITY from the installed directional association for each attempt; callers must not treat a retained DATA packet as a retained signed frame.

Verification: `test/unit/wire/test_wire.cpp`.

## Reserved payload transforms

Compression and payload encryption are reserved capabilities. Enabling either in the configuration returns `XGL_ERR_INVALID_PARAM`. A send with nonzero `compression_id` returns `XGL_ERR_UNSUPPORTED`. For the authentication tag, see [Security](security.md). Applications that transform messages agree on a business payload format and check the encoded length before submission.
