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
| TIMESTAMP_EXT | TODO | Reserved/unclear timestamp metadata |
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
| TIMESTAMP_EXT | TODO | TODO(xgen-link): confirm TIMESTAMP_EXT value format and whether it is reserved or intentionally unimplemented. |
| DATA_TYPE_EXT | 1 | `data_type u8` |

ACK ranges use `gap` and `length` to describe acknowledged ranges backwards from `largest_ack`. SACK bitmap bits describe receive state for `base_packet + bit_index`.
ACK_RANGE_EXT and SACK_EXT live in the header TLV area, not in payload.
An all-zero SACK bitmap is valid and requests fast retransmission of `base_packet`.
Application `data_type` values are not reserved by transport control values. Receivers interpret DATA_TYPE_EXT as a control subtype only when `packet_type=CONTROL`.

## Ownership and Traceability

| Extension | Type | Producer | Consumer | Failure rule | Evidence |
| --- | ---: | --- | --- | --- | --- |
| SESSION_EXT | 1 | Session-aware send paths | Datalink replay, network metadata, transport peer scope, fragment reassembly | Invalid length fails decode | `src/wire/xgl_wire_ext.c`, `test/test_wire.cpp`, `test/test_transport.cpp` |
| ACK_RANGE_EXT | 2 | `transport_send_ack()` | Reliable queue ACK removal | Invalid length or range count fails decode | `src/wire/xgl_wire_ack_ext.c`, `src/transport/xgl_transport_ack.c`, `test/test_reliable.cpp` |
| SACK_EXT | 3 | `transport_send_sack()` | SACK processing and fast retransmit | Invalid bitmap length fails decode | `src/wire/xgl_wire_ack_ext.c`, `src/transport/xgl_transport_sack.c`, `test/test_transport.cpp` |
| FRAGMENT_EXT | 4 | Fragment send path | Fragment manager and delivery path | Missing/invalid value prevents reassembly | `src/transport/xgl_transport_send_fragment.c`, `src/transport/xgl_transport_delivery.c`, `test/test_fragment.cpp` |
| SECURITY_EXT | 5 | Authenticated frame build | Parser auth length, datalink verification | AUTHENTICATED without valid SECURITY_EXT fails closed | `src/wire/xgl_frame_auth.c`, `src/wire/xgl_parser_extensions.c`, `test/test_datalink.cpp` |
| ROUTE_EXT | 6 | Route-aware internals/future route controls | Network metadata/routing | Invalid length fails decode | `src/wire/xgl_wire_ext.c`, `test/test_wire.cpp`, `test/test_network.cpp` |
| TIMESTAMP_EXT | 7 | TODO | TODO | TODO(xgen-link): confirm TIMESTAMP_EXT value format and whether it is reserved or intentionally unimplemented. | `include/xgl/internal/xgl_wire.h` |
| DATA_TYPE_EXT | 8 | Send, zero-copy, control packets | Network metadata and transport control | Value is one byte; control interpretation requires `packet_type=CONTROL` | `src/network/xgl_network_send.c`, `src/network/xgl_network_metadata.c`, `src/transport/xgl_transport_control.c` |

## Failure Rules

- Drop the frame when `ext_len` is too small for the declared extension.
- Extension bytes must fit within `header_len - 24`.
- Unknown extensions may be ignored only when they do not affect packet semantics; unknown security or fragmentation extensions should fail closed.

## Implementation Constraints

- Extension order must not be a semantic dependency; receivers find required extensions by type.
- Duplicate semantic extensions should fail closed to avoid ambiguity.
- Senders set HAS_EXTENSIONS only when extensions are present.
