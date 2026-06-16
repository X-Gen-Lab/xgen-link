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

## CRC 范围

`header_crc16` 只覆盖 24-byte 基础头，CRC 字段本身按零值参与计算。TLV 扩展、payload 和认证 trailer 的完整性由 frame CRC 覆盖；认证开启时还由 auth tag 覆盖。

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
