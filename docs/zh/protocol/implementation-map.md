# 实现映射

本页把协议设计映射到源码目录、公共头、关键函数和测试。读者可以先从这里理解“哪个模块实现哪条协议规则”，再进入具体页面阅读字段和状态机。

## 分层到目录

| 协议层 | 源码目录 | 公共/高级头 | 主要实现 | 关键不变量 |
| --- | --- | --- | --- | --- |
| API | `src/api` | `xgl.h`, `xgl_config.h`, `xgl_types.h` | `xgl_instance.c`, `xgl_send.c`, `xgl_stats.c`, `xgl_config.c` | 用户只通过 handle、config、send、run、stats 进入协议栈 |
| Wire | `src/wire` | `xgl/internal/xgl_wire.h`, `xgl/internal/xgl_frame.h`, `xgl/internal/xgl_parser.h` | `xgl_wire.c`, `xgl_frame.c`, `xgl_parser.c`, `xgl_crc.c` | wire 编解码按 offset 手写，不依赖 packed struct |
| Security | `src/security` | `xgl/internal/xgl_security.h` | `xgl_security.c` | replay window 按 source、connection、session、packet 隔离 |
| Datalink | `src/datalink` | `xgl/internal/xgl_datalink.h` | `xgl_datalink.c`, `xgl_datalink_send.c` | 未通过 CRC/auth/replay 的 frame 不进入 network |
| Network | `src/network` | `xgl/internal/xgl_network.h`, `xgl/internal/xgl_route.h` | `xgl_network.c`, `xgl_network_send.c`, `xgl_network_receive.c`, `xgl_network_metadata.c`, `xgl_route.c` | route、TTL、MTU 和 forwarding 在 network 层闭环 |
| Transport | `src/transport` | `xgl/internal/xgl_transport.h`, `xgl/internal/xgl_reliable.h`, `xgl/internal/xgl_window.h`, `xgl/internal/xgl_fragment.h`, `xgl/internal/xgl_rtt.h` | `xgl_transport.c`, `xgl_transport_send.c`, `xgl_transport_receive.c`, `xgl_transport_peer.c`, `xgl_transport_control.c`, `xgl_transport_ack.c`, `xgl_transport_retransmit.c`, `xgl_transport_rx_order.c`, `xgl_reliable.c`, `xgl_window.c`, `xgl_fragment.c`, `xgl_rtt.c` | 可靠状态按 peer key 隔离，有序交付给应用 |
| Memory | `src/memory` | allocator/pool 头 | allocator、mempool、packet_pool、tiered_pool | production/noheap profile 不应隐式回退到堆 |
| Platform | `src/platform` | time/mutex/atomic/platform 头 | time、mutex、atomic、platform hooks | ISR 只入队，协议处理在 task/main loop |

## TX 主路径

| 步骤 | 代码位置 | 责任 | 失败条件 |
| --- | --- | --- | --- |
| 参数检查 | `src/api/xgl_send.c` | 检查 handle、payload、target、长度、zero-copy 约束 | NULL、长度超限、认证 zero-copy 预留不足 |
| peer/packet number | `src/transport/xgl_transport_peer.c`, `src/transport/xgl_transport_send.c` | 按 peer key 创建状态，分配 32-bit packet number | peer pool/allocator 失败、窗口满 |
| 分片 | `src/transport/xgl_fragment.c` | 为超 MTU payload 生成 `FRAGMENT_EXT` 元数据 | message 过大、预算不足 |
| reliable queue | `src/transport/xgl_reliable.c` | 保存待 ACK 包，支持 ACK range/SACK 查找和重传 | reliable 队列满、内存不足 |
| route lookup | `src/network/xgl_network_send.c`, `src/network/xgl_route.c` | 查找目标 route，检查 route MTU | 无路由、MTU 不足、TTL 无效 |
| frame build | `src/wire/xgl_frame.c` | 构造 v2 header、TLV、payload、CRC/auth trailer | header/ext 长度越界、auth provider 缺失 |
| PHY send | `src/datalink/xgl_datalink_send.c` | 发送 serialized frame | PHY 返回错误 |

## RX 主路径

| 步骤 | 代码位置 | 责任 | 失败策略 |
| --- | --- | --- | --- |
| byte stream parser | `src/wire/xgl_parser.c` | magic resync、base header、TLV、payload、trailer 分阶段收包 | reset parser，继续寻找下一帧 |
| header/TLV decode | `src/wire/xgl_wire.c` | 校验 offset、长度、CRC、扩展合法性 | 丢弃，不交付 |
| auth/replay | `src/datalink/xgl_datalink.c`, `src/security/xgl_security.c` | 验证 tag，将 replay 分类为新包、可靠重复包或拒绝 | 拒绝坏包；可靠重复包仅用于 transport ACK 恢复 |
| local or forward | `src/network/xgl_network_receive.c` | 本地交付或 TTL 递减后转发 | TTL/route/MTU/auth 重签失败则丢弃 |
| reliability | `src/transport/xgl_transport_receive.c`, `src/transport/xgl_transport_ack.c`, `src/transport/xgl_transport_rx_order.c`, `src/transport/xgl_transport_retransmit.c` | 处理 ACK/SACK、乱序缓存、重复包过滤 | 错误 connection/session 不污染其他 peer |
| reassembly | `src/transport/xgl_fragment.c` | 按 `(source, connection, session, message)` 重组 | 超预算、超时、重叠异常则清理 |
| app callback | `src/api/xgl_instance.c` | 将有序完整 payload 交付应用 | callback 不应阻塞协议主循环 |

## Wire 扩展归属

| Extension | 编解码 | 语义消费方 | 主要规则 |
| --- | --- | --- | --- |
| `SESSION_EXT` | `xgl_wire_encode/decode_session_ext_value` | datalink、network、transport、fragment | session epoch 参与 replay、peer、reassembly 隔离 |
| `ACK_RANGE_EXT` | `xgl_wire_encode/decode_ack_range_ext_value` | transport reliable | 一次释放多个 packet number |
| `SACK_EXT` | `xgl_wire_encode/decode_sack_ext_value` | transport reliable | 保留洞并触发快速重传 |
| `FRAGMENT_EXT` | `xgl_wire_encode/decode_fragment_ext_value` | fragment manager | payload 内不再放分片头 |
| `SECURITY_EXT` | `xgl_wire_encode/decode_security_ext_value` | frame、datalink、network | 标记 key、nonce/material、tag 长度 |
| `ROUTE_EXT` | `xgl_wire_encode/decode_route_ext_value` | network/routing | route epoch、上一跳、下一跳、metric |
| `DATA_TYPE_EXT` | `xgl_wire_encode_ext` | network、transport | DATA 包携带应用 data_type，CONTROL 包携带控制子类型，避免污染 packet_type |

## 公共 API 与内部 API

普通 SDK 用户应只依赖：

- `include/xgl/xgl.h`
- `include/xgl/xgl_config.h`
- `include/xgl/xgl_types.h`
- `include/xgl/xgl_error.h`

内部协议头位于 `include/xgl/internal`。协议维护、测试和高级集成才需要 wire/parser/reliable/window/fragment 等头；它们不属于稳定用户 ABI，也不会随 SDK 包安装。

## 测试映射

| 能力 | 主要测试 |
| --- | --- |
| 24-byte header/TLV | `test/test_wire.cpp`, `test/test_frame.cpp`, `test/property/test_frame_properties.cpp` |
| parser resync/畸形帧 | `test/test_parser.cpp`, `test/property/test_serialization_properties.cpp` |
| auth/replay | `test/test_security.cpp`, `test/test_datalink.cpp` |
| route/TTL/MTU | `test/test_network.cpp`, `test/test_route.cpp`, `test/property/test_network_properties.cpp` |
| reliable/ACK/SACK/window | `test/test_transport.cpp`, `test/test_reliable.cpp`, `test/test_window.cpp`, `test/property/test_transport_properties.cpp` |
| fragmentation/budget | `test/test_fragment.cpp`, `test/property/test_fragment_properties.cpp` |
| memory/noheap/footprint | `test/test_allocator.cpp`, `test/test_mempool.cpp`, `test/test_packet_pool.cpp`, `test/test_footprint.cpp`, `tools/noheap_smoke.c` |

## 维护规则

- 修改 wire 字段时，同步更新 `include/xgl/internal/xgl_wire.h`、`src/wire/xgl_wire.c`、wire format 文档和 offset 测试。
- 修改可靠性语义时，同步更新 peer key、ACK/SACK 文档、transport 测试和 release validation。
- 修改认证边界时，必须更新 security 文档、zero-copy 文档、datalink/network 测试。
- 修改配置默认值时，同步更新 config presets、quick start 和 Doxygen 公共 API。
