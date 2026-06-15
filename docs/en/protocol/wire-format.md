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

## CRC Coverage

`header_crc16` covers only the 24-byte base header with the CRC field treated as zero. TLV extensions, payload, and the authentication trailer are covered by the frame CRC; authenticated frames are also covered by the auth tag.

## Traffic Class

The top two `traffic_class` bits carry the reliability class: `NONE(0x00)`, `ACK_ELICITING(0x40)`, and `ACK_ONLY(0x80)`. `0x20` marks fragments, and the low three bits carry priority. ACK/FRAGMENT/CONTROL flags are fast-path markers and redundant validation hints; they do not replace the `traffic_class` category.

## Packet Type

| Value | Name | Description |
| ---: | --- | --- |
| 0 | INVALID | Invalid type, not valid on production wire |
| 1 | DATA | Application payload; application `data_type` is carried in DATA_TYPE_EXT |
| 2 | ACK | ACK range or SACK control packet |
| 3 | CONTROL | RESET, NACK, and other control semantics; control subtype is carried in DATA_TYPE_EXT |
| 4 | HANDSHAKE | Reserved for session/capability negotiation |
| 5 | ROUTE | Reserved routing control |
| 6 | PROBE | Reserved probing |
| 7 | CLOSE | Connection close |

## Flags

| Flag | Value | Description |
| --- | ---: | --- |
| ACK_ELICITING | `0x01` | Receiver should generate acknowledgement |
| HAS_EXTENSIONS | `0x02` | TLV extensions follow the base header |
| FRAGMENTED | `0x04` | Payload belongs to a fragmented message |
| ENCRYPTED | `0x08` | Reserved, rejected by the current production path |
| AUTHENTICATED | `0x10` | Frame carries an authentication trailer |
| CONTROL | `0x20` | Control semantics |

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
- When authentication is required, unauthenticated or invalid frames are not delivered upward.

## Design Constraints

- Public structs are not wire layout.
- All multi-byte fields are little-endian.
- The parser must handle arbitrary chunked input and consecutive frames.
- After dropping a bad frame, the parser must keep searching for the next magic sequence.
