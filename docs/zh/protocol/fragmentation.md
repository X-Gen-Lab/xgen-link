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

## 接收侧

接收侧先验证 frame，再按 reassembly key 定位 buffer。只有收到覆盖 `[0, message_len)` 的连续范围后才组装完整 payload 并交给 transport。

## Budget

重组预算分两层：

- per-peer budget
- global budget

超过预算时丢弃分片并计数。RESET 只清理目标 connection/session 的重组状态。

## 攻击面

分片是内存消耗攻击的主要入口。生产配置必须限制最大消息长度、并发重组数量、per-peer budget 和 global budget。
