# Implementation Map

This page maps protocol design to source directories, public headers, key functions, and tests. Start here when you need to understand where a protocol rule is implemented.

## Layers to Directories

| Layer | Source directory | Public/advanced headers | Main implementation | Invariant |
| --- | --- | --- | --- | --- |
| API | `src/api` | `xgl.h`, `xgl_config.h`, `xgl_types.h` | `xgl_instance.c`, `xgl_send.c`, `xgl_stats.c`, `xgl_config.c` | Users enter through handle, config, send, run, and stats APIs |
| Wire | `src/wire` | `xgl/internal/xgl_wire.h`, `xgl/internal/xgl_frame.h`, `xgl/internal/xgl_parser.h` | `xgl_wire.c`, `xgl_frame.c`, `xgl_parser.c`, `xgl_crc.c` | Wire encoding is offset-based, not packed-struct based |
| Security | `src/security` | `xgl/internal/xgl_security.h` | `xgl_security.c` | Replay windows are scoped by source, connection, session, and packet |
| Datalink | `src/datalink` | `xgl/internal/xgl_datalink.h` | `xgl_datalink.c` | Frames that fail CRC/auth/replay do not enter network |
| Network | `src/network` | `xgl/internal/xgl_network.h`, `xgl/internal/xgl_route.h` | `xgl_network.c`, `xgl_route.c` | Routing, TTL, MTU, and forwarding are resolved here |
| Transport | `src/transport` | `xgl/internal/xgl_transport.h`, `xgl/internal/xgl_reliable.h`, `xgl/internal/xgl_window.h`, `xgl/internal/xgl_fragment.h`, `xgl/internal/xgl_rtt.h` | `xgl_transport.c`, `xgl_transport_peer.c`, `xgl_transport_control.c`, `xgl_transport_ack.c`, `xgl_transport_retransmit.c`, `xgl_transport_rx_order.c`, `xgl_reliable.c`, `xgl_window.c`, `xgl_fragment.c`, `xgl_rtt.c` | Reliable state is scoped by peer key and delivered in order |
| Memory | `src/memory` | allocator/pool headers | allocator, mempool, packet pool, tiered pool | Production/no-heap profiles must not silently fall back to heap |
| Platform | `src/platform` | time/mutex/atomic/platform headers | time, mutex, atomic, platform hooks | ISRs enqueue only; protocol work runs in task/main loop |

## TX Path

| Step | Code | Responsibility | Failure conditions |
| --- | --- | --- | --- |
| Parameter checks | `src/api/xgl_send.c` | Validate handle, payload, target, length, zero-copy constraints | NULL, oversized payload, insufficient authenticated zero-copy reservation |
| Peer/packet number | `src/transport/xgl_transport_peer.c`, `src/transport/xgl_transport.c` | Create scoped peer state and assign 32-bit packet numbers | peer allocation failure, full window |
| Fragmentation | `src/transport/xgl_fragment.c` | Create `FRAGMENT_EXT` metadata for oversized payloads | message too large, budget exceeded |
| Reliable queue | `src/transport/xgl_reliable.c` | Retain packets until ACKed and support ACK/SACK lookup | queue full, allocation failure |
| Route lookup | `src/network/xgl_network.c`, `src/network/xgl_route.c` | Find egress route and enforce route MTU | no route, MTU exceeded, invalid TTL |
| Frame build | `src/wire/xgl_frame.c` | Build v2 header, TLVs, payload, CRC/auth trailer | header/ext overflow, missing auth provider |
| PHY send | `src/datalink/xgl_datalink.c` | Send serialized frame | PHY error |

## RX Path

| Step | Code | Responsibility | Failure policy |
| --- | --- | --- | --- |
| Byte-stream parser | `src/wire/xgl_parser.c` | Resync on magic and collect header, TLVs, payload, trailer | reset parser and continue searching |
| Header/TLV decode | `src/wire/xgl_wire.c` | Validate offsets, lengths, CRC, extension encoding | drop, do not deliver |
| Auth/replay | `src/datalink/xgl_datalink.c`, `src/security/xgl_security.c` | Verify tag and classify replay as new, reliable duplicate, or reject | reject bad frames; allow reliable duplicates only for transport ACK recovery |
| Local or forward | `src/network/xgl_network.c` | Deliver locally or decrement TTL and forward | drop on TTL, route, MTU, or resign failure |
| Reliability | `src/transport/xgl_transport_ack.c`, `src/transport/xgl_transport_rx_order.c`, `src/transport/xgl_transport_retransmit.c` | Process ACK/SACK, buffer out-of-order packets, filter duplicates | wrong connection/session does not pollute other peers |
| Reassembly | `src/transport/xgl_fragment.c` | Reassemble by source, connection, session, and message | clean up on budget, timeout, or invalid overlap |
| App callback | `src/api/xgl_instance.c` | Deliver ordered complete payload | callback must not block the protocol loop |

## Extension Ownership

| Extension | Encoding | Semantic consumer | Main rule |
| --- | --- | --- | --- |
| `SESSION_EXT` | `xgl_wire_encode/decode_session_ext_value` | datalink, transport, fragment | Session epoch scopes replay, peer, and reassembly state |
| `ACK_RANGE_EXT` | `xgl_wire_encode/decode_ack_range_ext_value` | transport reliable | Releases multiple packet numbers |
| `SACK_EXT` | `xgl_wire_encode/decode_sack_ext_value` | transport reliable | Preserves holes and triggers fast retransmit |
| `FRAGMENT_EXT` | `xgl_wire_encode/decode_fragment_ext_value` | fragment manager | Fragment metadata is not stored inside payload |
| `SECURITY_EXT` | `xgl_wire_encode/decode_security_ext_value` | frame, datalink, network | Carries key, nonce/material, and tag length metadata |
| `ROUTE_EXT` | `xgl_wire_encode/decode_route_ext_value` | network/routing | Carries route epoch, previous hop, next hop, and metric |
| `DATA_TYPE_EXT` | `xgl_wire_encode_ext` | network, transport | Carries application data_type on DATA packets or control subtype on CONTROL packets without overloading packet_type |

## Public vs Internal API

Normal SDK users should depend on:

- `include/xgl/xgl.h`
- `include/xgl/xgl_config.h`
- `include/xgl/xgl_types.h`
- `include/xgl/xgl_error.h`

Internal protocol headers live under `include/xgl/internal`. Wire, parser, reliable, window, and fragment headers are for protocol maintenance, tests, or advanced integrations. They should not be documented as stable user ABI and are not installed by the SDK package.

## Test Mapping

| Capability | Primary tests |
| --- | --- |
| 24-byte header/TLV | `test/test_wire.cpp`, `test/test_frame.cpp`, `test/property/test_frame_properties.cpp` |
| Parser resync/malformed frames | `test/test_parser.cpp`, `test/property/test_serialization_properties.cpp` |
| Auth/replay | `test/test_security.cpp`, `test/test_datalink.cpp` |
| Route/TTL/MTU | `test/test_network.cpp`, `test/test_route.cpp`, `test/property/test_network_properties.cpp` |
| Reliable/ACK/SACK/window | `test/test_transport.cpp`, `test/test_reliable.cpp`, `test/test_window.cpp`, `test/property/test_transport_properties.cpp` |
| Fragmentation/budget | `test/test_fragment.cpp`, `test/property/test_fragment_properties.cpp` |
| Memory/no-heap/footprint | `test/test_allocator.cpp`, `test/test_mempool.cpp`, `test/test_packet_pool.cpp`, `test/test_footprint.cpp`, `tools/noheap_smoke.c` |

## Maintenance Rules

- Wire field changes must update `include/xgl/internal/xgl_wire.h`, `src/wire/xgl_wire.c`, wire format docs, and offset tests.
- Reliability semantic changes must update peer key docs, ACK/SACK docs, transport tests, and release validation.
- Authentication boundary changes must update security docs, zero-copy docs, and datalink/network tests.
- Config default changes must update config presets, quick start, and Doxygen public API.
