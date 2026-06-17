# Glossary

This glossary defines protocol terms that must stay consistent across English
and Chinese documentation.

| Term | Chinese | Meaning | Source of truth |
| --- | --- | --- | --- |
| wire format | wire format / 线格式 | Serialized bytes on the physical link | `include/xgl/internal/xgl_wire.h`, `docs/en/protocol/wire-format.md` |
| frame | 帧 | Complete serialized unit: base header, TLVs, payload, optional auth trailer, frame CRC | `include/xgl/internal/xgl_frame.h` |
| packet | 包 | Internal transport/network semantic object before or after serialization | `include/xgl/xgl_types.h`, `include/xgl/internal/xgl_packet_pool.h` |
| packet number | 包号 | 32-bit monotonic number used by reliability, ordering, and replay | `include/xgl/internal/xgl_wire.h` |
| remote peer id | 远端 peer ID | The peer identifier for scoped transport state; TX uses target, RX/ACK uses source | `src/transport/xgl_transport_peer.c` |
| connection scope | 连接作用域 | State partition keyed by `connection_id` | `include/xgl/xgl_types.h` |
| session epoch | session epoch | Reboot/session partition used by replay, peer, and reassembly state | `include/xgl/internal/xgl_wire.h` |
| ACK range | ACK range / 确认范围 | TLV acknowledgement format that can release multiple packet numbers | `src/wire/xgl_wire_ack_ext.c` |
| SACK | SACK / 选择性确认 | Bitmap acknowledgement that preserves holes and supports fast retransmit | `src/transport/xgl_transport_sack.c` |
| AAD | AAD / 附加认证数据 | Header and TLV bytes authenticated separately from payload | `src/wire/xgl_wire.c` |
| auth trailer | 认证 trailer | Authentication tag after payload and before frame CRC | `src/wire/xgl_frame_auth.c` |
| frame CRC | frame CRC | CRC16 over serialized frame bytes before the trailing CRC field | `src/wire/xgl_frame.c` |
| route MTU | route MTU | Maximum complete serialized frame length for a route | `include/xgl/xgl_types.h` |
| fragment budget | 分片预算 | Payload capacity left after route MTU subtracts headers, TLVs, auth, and CRC | `src/transport/xgl_transport_send_plan.c` |

When a term changes, update this page, the matching Chinese page, and any
protocol page that relies on the term.
