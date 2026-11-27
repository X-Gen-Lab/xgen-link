# Wire Format

The wire version is `1`. Integer fields are little-endian unless explicitly stated otherwise. A frame is `base header | TLVs | payload | optional authentication tag | frame CRC16`. The fixed header is 24 bytes; `header_len` includes all TLVs and is at most 255.

## Base Header

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 2 | Magic `A5 5A` |
| 2 | 1 | Version `01` |
| 3 | 1 | Total header length |
| 4 | 1 | Packet type |
| 5 | 1 | Flags |
| 6 | 1 | TTL |
| 7 | 1 | Traffic class |
| 8 | 2 | Source node ID |
| 10 | 2 | Destination node ID |
| 12 | 4 | Connection ID |
| 16 | 4 | Reliable DATA packet number |
| 20 | 2 | Payload length, excluding tag and CRC |
| 22 | 2 | Header CRC16 |

Node zero is invalid; `0xFFFF` is broadcast. DATA, ACK and CONTROL use types `1`, `2` and `3`, respectively. Wire rejects all other packet types. DATA_TYPE is a separate extension, so an application type cannot accidentally become a control packet. Reliable broadcast is rejected at endpoint admission; unreliable broadcast remains subject to routing and authentication policy.

## Flags and Traffic Class

The flag byte uses `0x01` for ACK elicitation, `0x02` to indicate extensions, `0x04` to indicate an authentication tag, and `0x08` for control. Unassigned flag bits must be clear.

The traffic-class byte uses bits 0–2 for priority and bits 6–7 for reliability. The supported reliability values are `0x00` (none), `0x40` (ACK eliciting) and `0x80` (ACK only). Bits 3–5 are unassigned and must be clear.

The extension flag is set exactly when `header_len > 24`. The packet type determines the permitted reliability value, flags and header fields.

| Type | Reliability | ACK-eliciting flag | Control flag | Packet number and payload |
| --- | --- | --- | --- | --- |
| Unreliable DATA | `0x00` | Clear | Clear | Packet number is zero |
| Reliable DATA | `0x40` | Set | Clear | Header packet number identifies the reliable DATA |
| ACK | `0x80` | Clear | Clear | Packet number and payload length are zero |
| CONTROL | `0x00` | Clear | Set | Packet number and payload length are zero |

## Validation and Numbering

Both CRCs use CRC-16/MODBUS and are encoded little-endian. Header CRC covers the 24-byte header with bytes 22 and 23 zeroed. Frame CRC covers every preceding frame byte, including TLVs and the authentication tag. The decoder validates complete boundaries, exact payload/tag lengths and DATA_TYPE, SESSION and SECURITY singleton constraints before returning borrowed spans. Complete-frame decoding and incremental stream parsing share Wire field, length and CRC checks. The version field must be `01`. Structurally valid unknown noncritical TLVs are skipped. A wire-authenticated flag indicates a tag is present; only security verification establishes authenticity.

Reliable DATA starts at packet number zero per peer scope and increments only after accepted initial submission. Retransmission preserves it. Usable DATA numbers end at `UINT32_MAX - 1`; both sending and receiving reject `UINT32_MAX` for reliable DATA, so receive history cannot wrap to zero. ACK, CONTROL and unreliable DATA encode zero; acknowledged numbers occur only inside ACK_RANGE or SACK. A connection must move to a new epoch before the reliable sequence is exhausted.

## Size Budget

Payload budget is `MTU - 24 - TLV bytes - tag bytes - 2`. A single-range ACK with SESSION, SECURITY and a 16-byte tag uses `24 + 15 + 14 + 15 + 16 + 2 = 86` bytes. A 128-byte control reservation accommodates that example. Current Boot builds omit authentication; the example is a wire budget, not a claim that authenticated Boot fits the same RAM configuration.

Verification: `test/unit/wire/test_wire.cpp` and `test/unit/transport/test_transport_rx_contracts.cpp`.
