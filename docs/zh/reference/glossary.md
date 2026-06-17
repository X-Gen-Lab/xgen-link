# 术语表

本页定义中英文文档中必须保持一致的协议术语。

| Term | 中文 | 含义 | 事实来源 |
| --- | --- | --- | --- |
| wire format | wire format / 线格式 | 物理链路上的序列化字节 | `include/xgl/internal/xgl_wire.h`, `docs/zh/protocol/wire-format.md` |
| frame | 帧 | 完整序列化单元：基础头、TLV、payload、可选 auth trailer、frame CRC | `include/xgl/internal/xgl_frame.h` |
| packet | 包 | 序列化前后在 transport/network 内部使用的语义对象 | `include/xgl/xgl_types.h`, `include/xgl/internal/xgl_packet_pool.h` |
| packet number | 包号 | 用于可靠性、ordering 和 replay 的 32-bit 单调编号 | `include/xgl/internal/xgl_wire.h` |
| remote peer id | 远端 peer ID | scoped transport state 的 peer 标识；TX 用 target，RX/ACK 用 source | `src/transport/xgl_transport_peer.c` |
| connection scope | 连接作用域 | 由 `connection_id` 隔离的状态分区 | `include/xgl/xgl_types.h` |
| session epoch | session epoch | replay、peer 和 reassembly 使用的 reboot/session 分区 | `include/xgl/internal/xgl_wire.h` |
| ACK range | ACK range / 确认范围 | 可一次释放多个 packet number 的 TLV 确认格式 | `src/wire/xgl_wire_ack_ext.c` |
| SACK | SACK / 选择性确认 | 保留缺口并支持 fast retransmit 的 bitmap 确认 | `src/transport/xgl_transport_sack.c` |
| AAD | AAD / 附加认证数据 | 与 payload 分开认证的 header 和 TLV bytes | `src/wire/xgl_wire.c` |
| auth trailer | 认证 trailer | 位于 payload 之后、frame CRC 之前的 authentication tag | `src/wire/xgl_frame_auth.c` |
| frame CRC | frame CRC | 对 trailing CRC 字段之前的 serialized frame bytes 计算的 CRC16 | `src/wire/xgl_frame.c` |
| route MTU | route MTU | route 允许的完整 serialized frame 最大长度 | `include/xgl/xgl_types.h` |
| fragment budget | 分片预算 | route MTU 扣除 headers、TLV、auth 和 CRC 后剩余的 payload 容量 | `src/transport/xgl_transport_send_plan.c` |

术语变更时，必须同步本页、英文对应页，以及依赖该术语的协议页面。
