# TLV Extensions

Extensions immediately follow the 24-byte base header.

| Field | Size | Description |
| --- | ---: | --- |
| `ext_type` | 1 | Extension type |
| `ext_len` | 1 | Value length |
| `value` | `ext_len` | Extension value |

## Extension Types

| Extension | Contents | Purpose |
| --- | --- | --- |
| SESSION_EXT | `session_epoch`, `incarnation_id` | Session isolation and reboot detection |
| ACK_RANGE_EXT | `largest_ack`, `ack_delay_us`, ranges | Batch acknowledgement |
| SACK_EXT | `base_packet`, bitmap | Preserve acknowledgement holes |
| FRAGMENT_EXT | `message_id`, `fragment_offset`, `message_len` | Fragment reassembly |
| SECURITY_EXT | `key_id`, nonce/material metadata | Authentication trailer metadata |
| ROUTE_EXT | previous hop, next hop, route epoch, metric | Routing metadata |
| DATA_TYPE_EXT | `data_type` | Application payload class on DATA packets; transport control subtype on CONTROL packets |

## Value Format

| Extension | Value Length | Fields |
| --- | ---: | --- |
| SESSION_EXT | 12 | `session_epoch u32`, `incarnation_id u64` |
| ACK_RANGE_EXT | `9 + 4*n` | `largest_ack u32`, `ack_delay_us u32`, `range_count u8`, repeated `gap u16 + length u16` |
| SACK_EXT | `5 + bitmap_len` | `base_packet u32`, `bitmap_len u8`, bitmap bytes |
| FRAGMENT_EXT | 12 | `message_id u32`, `fragment_offset u32`, `message_len u32` |
| SECURITY_EXT | 13 | `key_id u32`, `nonce_id u64`, `tag_len u8` |
| ROUTE_EXT | 10 | `previous_hop u16`, `next_hop u16`, `route_epoch u32`, `metric u16` |
| DATA_TYPE_EXT | 1 | `data_type u8` |

ACK ranges use `gap` and `length` to describe acknowledged ranges backwards from `largest_ack`. SACK bitmap bits describe receive state for `base_packet + bit_index`.
ACK_RANGE_EXT and SACK_EXT live in the header TLV area, not in payload.
An all-zero SACK bitmap is valid and requests fast retransmission of `base_packet`.
Application `data_type` values are not reserved by transport control values. Receivers interpret DATA_TYPE_EXT as a control subtype only when `packet_type=CONTROL`.

## Failure Rules

- Drop the frame when `ext_len` is too small for the declared extension.
- Extension bytes must fit within `header_len - 24`.
- Unknown extensions may be ignored only when they do not affect packet semantics; unknown security or fragmentation extensions should fail closed.

## Implementation Constraints

- Extension order must not be a semantic dependency; receivers find required extensions by type.
- Duplicate semantic extensions should fail closed to avoid ambiguity.
- Senders set HAS_EXTENSIONS only when extensions are present.
