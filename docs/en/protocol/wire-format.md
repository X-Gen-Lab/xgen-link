# Wire Format

Wire version is `3`. Integer fields are little-endian unless explicitly stated otherwise. A frame is `base header | TLVs | payload | optional authentication tag | frame CRC16`. The fixed header is 24 bytes; `header_len` includes all TLVs and is at most 255.

## Base Header

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 2 | Magic `A5 5A` |
| 2 | 1 | Version `03` |
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

Node zero is invalid; `0xFFFF` is broadcast. DATA, ACK and CONTROL use types 1, 2 and 3. Other declared packet types reserve encoding values and do not define additional application services. DATA_TYPE is a separate extension, so an application type cannot accidentally become a control packet.

## Validation and Numbering

Both CRCs use CRC-16/MODBUS and are encoded little-endian. Header CRC covers the 24-byte header with bytes 22 and 23 zeroed. Frame CRC covers every preceding frame byte, including TLVs and the authentication tag. The decoder validates complete boundaries, exact payload/tag lengths and singleton TLVs before returning borrowed spans. A wire-authenticated flag indicates a tag is present; only security verification establishes authenticity.

Reliable DATA starts at packet number zero per peer scope and increments only after accepted initial submission. Retransmission preserves it. ACK, CONTROL and unreliable DATA encode zero; acknowledged numbers occur only inside ACK_RANGE or SACK. A connection must move to a new epoch before the reliable sequence is exhausted.

## Size Budget

Payload budget is `MTU - 24 - TLV bytes - tag bytes - 2`. A single-range ACK with SESSION, SECURITY and a 16-byte tag uses `24 + 15 + 14 + 15 + 16 + 2 = 86` bytes. A 128-byte control reservation accommodates that example. Current Boot builds omit authentication; the example is a wire budget, not a claim that authenticated Boot fits the same RAM configuration.

Verification: `test/test_wire.cpp`.
