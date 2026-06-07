# Send API

`xgl_send()` 是默认发送 API。

## 数据所有权

调用期间借用 `tx_data->data`。可靠发送需要重传，因此协议会保留稳定副本或等价存储。

## Reliable 与 Unreliable

| 模式 | 行为 |
| --- | --- |
| unreliable | 尽快发送，不等待 ACK |
| reliable | 分配 packet number，进入 reliable queue，等待 ACK range/SACK |

## Fragmentation

payload 超过 route MTU 时，如果 fragmentation 开启，会使用 FRAGMENT_EXT 分片。否则发送失败。

## Callback

应用 callback 只接收已通过 CRC、认证、路由和可靠性处理的 payload。

## 发送失败处理

| 错误 | 常见原因 | 处理 |
| --- | --- | --- |
| `XGL_ERR_ROUTE_NOT_FOUND` | 没有目标 route | 检查 route table |
| `XGL_ERR_BUFFER_TOO_SMALL` | frame 超过 route MTU 或 caller buffer 不足 | 降低 payload、启用分片或增大 MTU |
| `XGL_ERR_WINDOW_FULL` | reliable window 满 | 稍后重试或调大窗口/队列预算 |
| `XGL_ERR_NO_MEMORY` | allocator/pool 不足 | 调整资源模型 |
| `XGL_ERR_TX_FAILED` | PHY 发送失败 | 检查驱动和链路 |

## 应用层 data_type

`data_type` 是应用 payload 分类，不参与可靠性排序。协议控制语义使用 packet type 和 TLV 扩展表达。
