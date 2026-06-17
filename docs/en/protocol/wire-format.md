# Wire Format

XGL v2 uses a fixed 24-byte base header. The wire path does not depend on packed structs or struct `memcpy`.

| Offset | Size | Field | Encoding | Description |
| --- | ---: | --- | --- | --- |
| 0 | 2 | `magic` | bytes | Fixed `A5 5A` |
| 2 | 1 | `version` | u8 | Fixed `2` |
| 3 | 1 | `header_len` | u8 | Base header plus TLV extensions |
| 4 | 1 | `packet_type` | u8 | DATA, ACK, CONTROL, and others |
| 5 | 1 | `flags` | u8 | ACK, EXT, FRAGMENT, AUTH, and others |
| 6 | 1 | `ttl` | u8 | Decremented on each hop |
| 7 | 1 | `traffic_class` | u8 | Priority and class bits |
| 8 | 2 | `source_id` | LE u16 | Source node |
| 10 | 2 | `target_id` | LE u16 | Target node |
| 12 | 4 | `connection_id` | LE u32 | Connection isolation |
| 16 | 4 | `packet_number` | LE u32 | Monotonic packet number |
| 20 | 2 | `payload_len` | LE u16 | Payload length |
| 22 | 2 | `header_crc16` | LE u16 | Header CRC |

## Field Lifecycle

| Field | TX source | Forwarding rule | RX validation | Evidence |
| --- | --- | --- | --- | --- |
| `magic` | `xgl_wire_encode_header()` writes `A5 5A` | Immutable | Parser resynchronizes on this byte pair | `include/xgl/internal/xgl_wire.h`, `src/wire/xgl_wire.c`, `test/test_parser.cpp` |
| `version` | Fixed protocol version `2` | Immutable | Header decode rejects unsupported versions | `include/xgl/internal/xgl_network.h`, `src/wire/xgl_wire.c`, `test/test_wire.cpp` |
| `header_len` | Base header plus serialized TLVs | Immutable after TX | Must be at least 24 and fit parser cache | `src/wire/xgl_wire.c`, `src/wire/xgl_parser_extensions.c`, `test/test_parser.cpp` |
| `packet_type` | Transport/network packet semantic | Immutable | Values outside DATA..CLOSE fail closed | `include/xgl/internal/xgl_wire.h`, `src/wire/xgl_wire.c`, `test/test_wire.cpp` |
| `flags` | Derived from reliability, extensions, fragmentation, auth, control | Immutable except future hop-local designs | SECURITY_EXT must exist when AUTHENTICATED is set | `src/wire/xgl_frame.c`, `src/wire/xgl_parser_extensions.c`, `test/test_parser.cpp` |
| `ttl` | `XGL_DEFAULT_TTL` on network TX | Decremented exactly once per forward | Remote frames with `ttl <= 1` are dropped | `src/network/xgl_network_send.c`, `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| `traffic_class` | Reliability class, fragment bit, priority | Immutable | Transport interprets ACK-eliciting/ACK-only classes | `include/xgl/xgl_types.h`, `src/transport`, `test/test_transport.cpp` |
| `source_id` | Local `config.source_id` unless packet overrides zero on internal send | Immutable | Cannot be 0 or `XGL_BROADCAST_ID` | `src/network/xgl_network.c`, `src/network/xgl_network_send.c`, `test/test_network.cpp` |
| `target_id` | `xgl_tx_data_t.target_id` or control target | Immutable | Local delivery when local or broadcast; otherwise route lookup | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| `connection_id` | TX connection scope, default 0 | Immutable | Scopes peer, replay, ACK, and fragment state | `src/transport/xgl_transport_peer.c`, `test/test_transport.cpp` |
| `packet_number` | Monotonic transport packet number | Immutable | Drives ACK/SACK, replay, ordering | `src/transport/xgl_transport_packet_number.c`, `test/test_reliable.cpp` |
| `payload_len` | Application payload or fragment payload length | Immutable | Parser uses it to derive body length | `src/wire/xgl_parser.c`, `test/test_parser.cpp` |
| `header_crc16` | Computed over base header with this field zeroed | Recomputed after TTL rewrite | Header decode recomputes and compares | `src/wire/xgl_wire.c`, `test/test_wire.cpp`, `test/test_crc.cpp` |

## CRC Coverage

`header_crc16` covers only the 24-byte base header with the CRC field treated as zero. TLV extensions, payload, and the authentication trailer are covered by the frame CRC; authenticated frames are also covered by the auth tag. End-to-end authentication uses a canonical AAD where hop-mutable TTL and header CRC are treated as zero before signing or verification.

## Traffic Class

The top two `traffic_class` bits carry the reliability class: `NONE(0x00)`, `ACK_ELICITING(0x40)`, and `ACK_ONLY(0x80)`. `0x20` marks fragments, and the low three bits carry priority. ACK/FRAGMENT/CONTROL flags are fast-path markers and redundant validation hints; they do not replace `packet_type` or the `traffic_class` category. ACK-only packets use `packet_type=ACK` and `traffic_class=ACK_ONLY`; they do not set the CONTROL flag.

## Packet Type

| Value | Name | Description |
| ---: | --- | --- |
| 0 | INVALID | Invalid type, not valid on production wire |
| 1 | DATA | Application payload; application `data_type` is carried in DATA_TYPE_EXT |
| 2 | ACK | ACK range or SACK control packet |
| 3 | CONTROL | HELLO, RESET, and other session control semantics; control subtype is carried in DATA_TYPE_EXT |
| 4 | HANDSHAKE | Reserved for session/capability negotiation |
| 5 | ROUTE | Reserved routing control |
| 6 | PROBE | Reserved probing |
| 7 | CLOSE | Connection close |

Packet types outside `1..7` are invalid on the production wire and fail closed during header decoding.

## Flags

| Flag | Value | Description |
| --- | ---: | --- |
| ACK_ELICITING | `0x01` | Receiver should generate acknowledgement |
| HAS_EXTENSIONS | `0x02` | TLV extensions follow the base header |
| FRAGMENTED | `0x04` | Payload belongs to a fragmented message |
| ENCRYPTED | `0x08` | Reserved, rejected by the current production path |
| AUTHENTICATED | `0x10` | Frame carries an authentication trailer |
| CONTROL | `0x20` | Redundant marker for `packet_type=CONTROL` frames |

## Full Frame Layout

```text
base header (24)
extensions (header_len - 24)
payload (payload_len)
auth trailer (optional)
frame crc16
```

`header_len` is the parser boundary inside the frame. Route MTU checks must use the complete serialized frame.

## Parser Behavior

- The parser resynchronizes on the two-byte binary magic `A5 5A`.
- Overlapping magic is supported, such as noise ending in `A5` followed by a valid `A5 5A`.
- `header_len < 24`, extension overrun, payload limit overflow, and CRC errors are dropped and counted.
- When authentication is required, unauthenticated frames are not delivered upward.
- When a frame declares authentication with AUTHENTICATED/SECURITY_EXT, the tag is verified even if authentication is optional for the instance.

## Design Constraints

- Public structs are not wire layout.
- All multi-byte fields are little-endian.
- The parser must handle arbitrary chunked input and consecutive frames.
- After dropping a bad frame, the parser must keep searching for the next magic sequence.
