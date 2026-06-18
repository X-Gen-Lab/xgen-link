# Wire Format

XGL v2 的基础头固定 24 bytes。wire path 不依赖 packed struct 或 `memcpy` struct。

| Offset | Size | 字段 | 编码 | 说明 |
| --- | ---: | --- | --- | --- |
| 0 | 2 | `magic` | bytes | 固定 `A5 5A` |
| 2 | 1 | `version` | u8 | 固定 `2` |
| 3 | 1 | `header_len` | u8 | 基础头 + TLV 扩展长度 |
| 4 | 1 | `packet_type` | u8 | DATA、ACK、CONTROL 等 |
| 5 | 1 | `flags` | u8 | ACK、EXT、FRAGMENT、AUTH 等 |
| 6 | 1 | `ttl` | u8 | 每跳递减 |
| 7 | 1 | `traffic_class` | u8 | 优先级和类别 |
| 8 | 2 | `source_id` | LE u16 | 源节点 |
| 10 | 2 | `target_id` | LE u16 | 目标节点 |
| 12 | 4 | `connection_id` | LE u32 | 连接隔离 |
| 16 | 4 | `packet_number` | LE u32 | 单调包号 |
| 20 | 2 | `payload_len` | LE u16 | payload 长度 |
| 22 | 2 | `header_crc16` | LE u16 | 头 CRC |

## 字段生命周期

| 字段 | TX 来源 | 转发规则 | RX 校验 | 证据 |
| --- | --- | --- | --- | --- |
| `magic` | `xgl_wire_encode_header()` 写入 `A5 5A` | 不变 | parser 通过该字节对重同步 | `include/xgl/internal/xgl_wire.h`, `src/wire/xgl_wire.c`, `test/test_parser.cpp` |
| `version` | 固定协议版本 `2` | 不变 | header decode 拒绝不支持的版本 | `include/xgl/internal/xgl_network.h`, `src/wire/xgl_wire.c`, `test/test_wire.cpp` |
| `header_len` | 基础头加已序列化 TLV | TX 后不变 | 必须至少为 24 且适配 parser cache | `src/wire/xgl_wire.c`, `src/wire/xgl_parser_extensions.c`, `test/test_parser.cpp` |
| `packet_type` | transport/network 包语义 | 不变 | DATA..CLOSE 之外的值 fail closed | `include/xgl/internal/xgl_wire.h`, `src/wire/xgl_wire.c`, `test/test_wire.cpp` |
| `flags` | 由可靠性、扩展、分片、认证、控制语义派生 | 除未来 hop-local 设计外不变 | 设置 AUTHENTICATED 时必须存在 SECURITY_EXT | `src/wire/xgl_frame.c`, `src/wire/xgl_parser_extensions.c`, `test/test_parser.cpp` |
| `ttl` | network TX 使用 `XGL_DEFAULT_TTL` | 每次转发只递减一次 | 远端帧 `ttl <= 1` 时丢弃 | `src/network/xgl_network_send.c`, `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| `traffic_class` | 可靠性类别、fragment bit、priority | 不变 | transport 解释 ACK-eliciting/ACK-only 类别 | `include/xgl/xgl_types.h`, `src/transport`, `test/test_transport.cpp` |
| `source_id` | 本地 `config.source_id`，内部发送为 0 时由 network 填充 | 不变 | 不能为 0 或 `XGL_BROADCAST_ID` | `src/network/xgl_network.c`, `src/network/xgl_network_send.c`, `test/test_network.cpp` |
| `target_id` | `xgl_tx_data_t.target_id` 或控制目标 | 不变 | 本地或 broadcast 交付，否则查 route | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| `connection_id` | TX connection scope，默认 0 | 不变 | 隔离 peer、replay、ACK 和 fragment 状态 | `src/transport/xgl_transport_peer.c`, `test/test_transport.cpp` |
| `packet_number` | transport 单调包号 | 不变 | 驱动 ACK/SACK、replay、ordering | `src/transport/xgl_transport_packet_number.c`, `test/test_reliable.cpp` |
| `payload_len` | 应用 payload 或 fragment payload 长度 | 不变 | parser 用它推导 body 长度 | `src/wire/xgl_parser.c`, `test/test_parser.cpp` |
| `header_crc16` | 按该字段置 0 的基础头计算 | TTL rewrite 后重算 | header decode 重算并比较 | `src/wire/xgl_wire.c`, `test/test_wire.cpp`, `test/test_crc.cpp` |

## CRC 范围

`header_crc16` 只覆盖 24-byte 基础头，CRC 字段本身按零值参与计算。TLV 扩展、payload 和认证 trailer 的完整性由 frame CRC 覆盖；认证开启时还由 auth tag 覆盖。端到端认证使用 canonical AAD：签名或验签前，逐跳可变的 TTL 和 header CRC 按零值处理。

## Traffic Class

`traffic_class` 的高两位表示可靠性类别：`NONE(0x00)`、`ACK_ELICITING(0x40)`、`ACK_ONLY(0x80)`。`0x20` 表示分片，低三位表示优先级。`flags` 中的 ACK/FRAGMENT/CONTROL 位用于快速判断和冗余校验，不能替代 `packet_type` 或 `traffic_class` 的类别语义。ACK-only 包使用 `packet_type=ACK` 和 `traffic_class=ACK_ONLY`，不会设置 CONTROL flag。

## Packet Type

| 值 | 名称 | 说明 |
| ---: | --- | --- |
| 0 | INVALID | 非法类型，不应出现在生产帧 |
| 1 | DATA | 应用 payload；应用 `data_type` 放在 DATA_TYPE_EXT |
| 2 | ACK | ACK range 或 SACK 控制包 |
| 3 | CONTROL | HELLO、RESET 等会话控制语义；控制子类型放在 DATA_TYPE_EXT |
| 4 | HANDSHAKE | 会话/能力协商预留 |
| 5 | ROUTE | 路由控制预留 |
| 6 | PROBE | 探测预留 |
| 7 | CLOSE | 连接关闭 |

生产 wire 上只接受 `1..7` 的 packet type；其他值在 header decode 阶段 fail closed。

## Flags

| Flag | 值 | 说明 |
| --- | ---: | --- |
| ACK_ELICITING | `0x01` | 接收端应产生 ACK |
| HAS_EXTENSIONS | `0x02` | header 后有 TLV 扩展 |
| FRAGMENTED | `0x04` | payload 属于分片消息 |
| ENCRYPTED | `0x08` | 保留，当前生产路径拒绝 |
| AUTHENTICATED | `0x10` | frame 带认证 trailer |
| CONTROL | `0x20` | `packet_type=CONTROL` 帧的冗余标记 |

## 总帧布局

```text
base header (24)
extensions (header_len - 24)
payload (payload_len)
auth trailer (optional)
frame crc16
```

`header_len` 是 frame 内部解析边界；route MTU 判断必须基于完整 serialized frame。

## Parser 行为

- parser 通过双字节二进制 magic `A5 5A` 重同步。
- 支持重叠 magic，例如噪声末尾 `A5` 后接合法 `A5 5A`。
- `header_len < 24`、扩展越界、payload 超限、CRC 错误都会丢弃并计数。
- 认证要求开启时，未认证帧不会交付上层。
- 当帧通过 AUTHENTICATED/SECURITY_EXT 声明自己已认证时，即使实例未强制所有帧认证，也必须验签成功后才能交付。

## 设计约束

- 不允许把 public struct 直接作为 wire layout。
- 所有 multi-byte 字段都是 little-endian。
- parser 必须能处理任意分片输入和连续多帧输入。
- 错误帧丢弃后必须继续寻找下一组 magic，不阻塞后续数据。

## CRC16 算法参数

XGL 使用 CRC16 校验帧完整性。具体算法参数：

| 参数 | 值 | 说明 |
| --- | --- | --- |
| 算法 | CRC-16/MODBUS | 工业标准 CRC16 变体 |
| 多项式 | `0x8005` | x^16 + x^15 + x^2 + 1 |
| 初始值 | `0xFFFF` | 全 1 起始 |
| 输入反射 | 是 | 按字节查表,低位优先 |
| 输出反射 | 是 | 计算完成后结果按位反射 |
| 结果异或 | `0x0000` | 无最终异或 |

### CRC 覆盖范围

- `header_crc16`：仅覆盖 24-byte 基础头，CRC 字段本身按零值参与计算
- `frame_crc16`：覆盖完整帧（header + extensions + payload + auth trailer），CRC 字段本身按零值参与计算

### 使用场景

| 场景 | CRC 类型 | 证据 |
| --- | --- | --- |
| TX header 编码 | header_crc16 | `src/wire/xgl_wire.c` |
| RX header 验证 | header_crc16 重算比较 | `src/wire/xgl_wire.c`, `test/test_wire.cpp` |
| TX frame 构建 | frame_crc16 | `src/wire/xgl_frame.c` |
| RX frame 验证 | frame_crc16 重算比较 | `src/wire/xgl_parser.c`, `test/test_crc.cpp` |
| TTL 转发重写 | header_crc16 重算 | `src/network/xgl_network_receive.c` |
