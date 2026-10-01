# 扩展

每个 TLV 的布局为 `type:u8 | value_length:u8 | value`。TLV 受 `header_len` 限定，不从载荷内容推断边界。未知类型会跳过。重复的 DATA_TYPE、SESSION、SECURITY 单例扩展会被拒绝；transport 要求每个 ACK 恰好包含一个 ACK_RANGE 或 SACK。

## 已定义的值

| 类型 | 值长度 | 含义 |
| --- | --- | --- |
| SESSION = 1 | 12 | `epoch:u32`、`incarnation_id:u64` |
| ACK_RANGE = 2 | `9 + 4*n` | `largest_ack:u32`、`ack_delay_us:u32`、`count:u8`，后接 `gap:u16,length:u16` |
| SACK = 3 | `5 + n` | `base:u32`、`bitmap_length:u8`、位图 |
| FRAGMENT = 4 | 12 | `message_id:u32`、`offset:u32`、`total_length:u32` |
| SECURITY = 5 | 13 | `key_id:u32`、`security_seq:u64`、`tag_length:u8` |
| DATA_TYPE = 8 | 1 | 应用类型或 CONTROL 操作 |

值中的整数使用小端序。ROUTE、TIMESTAMP 编号保留。SESSION 增加 14 字节，FRAGMENT 增加 14 字节，SECURITY 增加 15 字节。未携带 SESSION 表示 epoch 为零。`incarnation_id` 是传输的元数据，其存在不会安装信任，也不会替代 peer key。

## ACK 语义

第一个 ACK 区间的 gap 必须为零，从 `largest_ack` 开始；length 为正的包数。后续区间跳过 gap 与区间边界，再描述较低包号。非法区间、下溢、未来包号使整个 ACK 被拒绝，在此之前不修改队列、窗口或 RTT。

SACK 第 `i` 位描述 `base + i`；低于 base 的包累计确认。置位表示接收方持有所对应的字节，缺失位可触发重传。当前发送端最多生成八个字节的位图。Boot 即使裁剪了乱序缓存，仍可处理 transport 支持的 ACK 形式。

## 组合

Network 组合或校验 DATA_TYPE 与非零 SESSION epoch 扩展。重传保留分片元数据，但重新构造帧。认证层每次尝试都从已安装的方向关联填充 SECURITY；保留 DATA 包不等于保留已经签名的帧。

验证依据：`test/unit/wire/test_wire.cpp`。
