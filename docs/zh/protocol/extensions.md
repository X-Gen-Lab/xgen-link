# TLV 扩展

扩展头紧跟 24-byte 基础头。

| 字段 | Size | 说明 |
| --- | ---: | --- |
| `ext_type` | 1 | 扩展类型 |
| `ext_len` | 1 | value 长度 |
| `value` | `ext_len` | 扩展负载 |

## 扩展类型

| 扩展 | 内容 | 用途 |
| --- | --- | --- |
| SESSION_EXT | `session_epoch`, `incarnation_id` | 会话隔离和重启识别 |
| ACK_RANGE_EXT | `largest_ack`, `ack_delay_us`, ranges | 批量确认 |
| SACK_EXT | `base_packet`, bitmap | 保留未确认洞 |
| FRAGMENT_EXT | `message_id`, `fragment_offset`, `message_len` | 分片重组 |
| SECURITY_EXT | `key_id`, nonce/material metadata | 认证 trailer 元数据 |
| ROUTE_EXT | previous hop、next hop、route epoch、metric | 路由信息 |
| DATA_TYPE_EXT | `data_type` | DATA 包上的应用 payload 分类；CONTROL 包上的 transport control 子类型 |

## Value 格式

| 扩展 | Value 长度 | 字段 |
| --- | ---: | --- |
| SESSION_EXT | 12 | `session_epoch u32`, `incarnation_id u64` |
| ACK_RANGE_EXT | `9 + 4*n` | `largest_ack u32`, `ack_delay_us u32`, `range_count u8`, repeated `gap u16 + length u16` |
| SACK_EXT | `5 + bitmap_len` | `base_packet u32`, `bitmap_len u8`, bitmap bytes |
| FRAGMENT_EXT | 12 | `message_id u32`, `fragment_offset u32`, `message_len u32` |
| SECURITY_EXT | 13 | `key_id u32`, `nonce_id u64`, `tag_len u8` |
| ROUTE_EXT | 10 | `previous_hop u16`, `next_hop u16`, `route_epoch u32`, `metric u16` |
| DATA_TYPE_EXT | 1 | `data_type u8` |

ACK range 的 `gap` 和 `length` 表示从 `largest_ack` 反向描述的确认区间。SACK bitmap 的 bit 表示 `base_packet + bit_index` 的接收状态。
ACK_RANGE_EXT 和 SACK_EXT 位于 header TLV 区，不放在 payload 中。
全零 SACK bitmap 是合法编码，表示请求快速重传 `base_packet`。
应用 `data_type` 不因 transport control 值而保留。接收端只有在 `packet_type=CONTROL` 时才把 DATA_TYPE_EXT 解释为控制子类型。

## 失败规则

- `ext_len` 不足以解码对应扩展时丢弃。
- 扩展总长度不能超过 `header_len - 24`。
- 未知扩展只有在不影响当前包语义时才可忽略；安全和分片相关未知扩展应 fail closed。

## 实现约束

- 扩展顺序不应成为语义依赖，接收端按 type 查找需要的扩展。
- 同一语义扩展重复出现时，接收端应选择 fail closed，避免歧义。
- 发送端只有在存在扩展时设置 HAS_EXTENSIONS。
