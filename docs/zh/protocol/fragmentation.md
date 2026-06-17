# 分片重组

分片元数据使用 FRAGMENT_EXT，不放在 payload 前缀中。

## Reassembly Key

```text
source_id + connection_id + session_epoch + message_id
```

相同 `message_id` 在不同 session 中不会混淆。

## Range 模型

重组管理器记录已收到的 byte ranges 或 chunk ranges，避免按每个字节扫描大 payload。重复范围可以忽略，冲突或越界范围必须 fail closed。

## 发送侧

发送侧根据 route MTU、基础头、扩展头、认证 trailer 和 frame CRC 计算每片 payload 容量。每片都带 FRAGMENT_EXT，并保留相同的 `message_id`、`message_len`、connection 和 session。

### Fragment Budget 公式

发送规划分两步推导 payload budget：

```text
app_payload_budget =
  route_max_frame_size
  - XGL_WIRE_BASE_HEADER_SIZE
  - app_extensions_len
  - auth_overhead
  - XGL_CRC16_SIZE

fragment_payload_budget =
  app_payload_budget - XGL_FRAGMENT_EXT_SIZE
```

当 `data_type != 0` 时，`app_extensions_len` 包含 DATA_TYPE_EXT。启用认证时，
`auth_overhead` 是 SECURITY_EXT 加 provider tag 长度。当
`data_len <= app_payload_budget` 时，packet 以单帧发送。需要分片时，每片都额外消耗
FRAGMENT_EXT 的 TLV 空间，因此 `fragment_payload_budget` 小于单帧 payload budget。

| Budget 项 | 来源 | 失败 |
| --- | --- | --- |
| Route MTU | `xgl_route_item_t.max_frame_size` | `XGL_ERR_BUFFER_TOO_SMALL` 或分片规划失败 |
| 基础头 | `XGL_WIRE_BASE_HEADER_SIZE` | 固定 24 bytes |
| DATA_TYPE_EXT | `data_type` 非零时使用 `XGL_DATA_TYPE_EXT_SIZE` | 单帧和分片发送都计入 |
| FRAGMENT_EXT | `XGL_FRAGMENT_EXT_SIZE` | 仅 fragment 必需 |
| Auth overhead | `XGL_SECURITY_EXT_SIZE + tag_len` | 需要合法 provider/tag length |
| Frame CRC | `XGL_CRC16_SIZE` | 始终存在 |

## 接收侧

接收侧先验证 frame，再按 reassembly key 定位 buffer。只有收到覆盖 `[0, message_len)` 的连续范围后才组装完整 payload 并交给 transport。

### Reassembly 规则

| 条件 | 行为 | 证据 |
| --- | --- | --- |
| key 的第一个合法 fragment | 分配 reassembly buffer 并预留 `message_len` 字节 | `src/transport/xgl_fragment_reassembly.c`, `test/test_fragment.cpp` |
| 非重叠 range | 拷贝字节并插入/合并 received range | `src/transport/xgl_fragment_range.c`, `test/test_fragment.cpp` |
| 重复 range | 仅当不会产生非法 overlap 时接受 | `src/transport/xgl_fragment_range.c`, `test/test_fragment.cpp` |
| `message_len == 0` | 拒绝 | `src/transport/xgl_fragment_process.c` |
| `fragment_offset > message_len` | 拒绝 | `src/transport/xgl_fragment_process.c` |
| `data_len > message_len - fragment_offset` | 拒绝 | `src/transport/xgl_fragment_process.c` |
| 已存在 key 但 `message_len` 不同 | 拒绝 | `src/transport/xgl_fragment_process.c` |
| 所有 range 覆盖 `[0, message_len)` | 移除 buffer 并交付完整 payload | `src/transport/xgl_fragment_process.c`, `test/test_fragment.cpp` |

## Budget

重组预算分两层：

- 最大并发 reassembly buffer 数
- 最大 message size
- 可选的 aggregate in-flight reassembly bytes

超过预算时丢弃分片并计数。RESET 通过 `xgl_fragment_clear_reassembly_scope()` 只清理目标 connection/session 的重组状态。

| Budget | 来源 | 清理 |
| --- | --- | --- |
| 并发 buffer | `max_reassembly_buffers` | 满时新 buffer 分配失败 |
| Message bytes | `max_message_size` | 超大 message 被拒绝 |
| Aggregate bytes | `max_reassembly_bytes` | in-flight reservation 防止耗尽 |
| Timeout | `reassembly_timeout_ms` | `xgl_fragment_process_timeouts()` 释放 stale buffer |
| Scope reset | peer + `connection_id` + `session_epoch` | RESET/CLOSE 只清理匹配 buffer |

## 攻击面

分片是内存消耗攻击的主要入口。生产配置必须限制最大消息长度、并发重组数量、per-peer budget 和 global budget。

## 追溯

| 规则 | 源码 | 测试 |
| --- | --- | --- |
| Fragment value 格式 | `src/wire/xgl_wire_ext.c` | `test/test_wire.cpp` |
| 发送预算规划 | `src/transport/xgl_transport_send_plan.c` | `test/test_transport.cpp` |
| Fragment 发送 | `src/transport/xgl_transport_send_fragment.c` | `test/test_transport.cpp` |
| Reassembly key 和 buffer | `src/transport/xgl_fragment_reassembly.c` | `test/test_fragment.cpp` |
| Range merge/overlap 规则 | `src/transport/xgl_fragment_range.c` | `test/test_fragment.cpp` |
| Timeout 和 scope cleanup | `src/transport/xgl_fragment_maintenance.c`, `src/transport/xgl_transport_peer.c` | `test/test_fragment.cpp`, `test/test_transport.cpp` |
