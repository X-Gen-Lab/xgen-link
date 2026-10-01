# 零拷贝

`xgl_send_zerocopy_at()` 在调用者提供的可写存储中构造单帧非可靠消息，不引入异步缓冲所有权。

## 支持范围

当前 API 只支持单帧非可靠发送。`reliable=true` 返回 `XGL_ERR_INVALID_PARAM`；可靠或分片消息使用 `xgl_send_at()`。

零拷贝描述符没有 connection/epoch 字段，使用默认 scope。认证发送要求已经为该 scope 安装可信会话。需要显式选择 connection/epoch 时使用普通发送 API。

## 缓冲布局

Payload 前预留精确的头部偏移，后面预留 trailer：

| 部分 | 字节数 |
| --- | ---: |
| 基础头 | `XGL_FRAME_HEADER_SIZE`（24） |
| 非零 data type | `XGL_DATA_TYPE_EXT_SIZE`（3） |
| 启用认证时的 security extension | 15 |
| 启用认证时的 tag | Provider tag 长度 |
| 末尾 CRC | `XGL_CRC16_SIZE`（2） |

`data_offset` 必须等于基础头加启用的扩展。`buffer_size` 须在不溢出的前提下覆盖 offset、payload、tag 和 CRC，完整序列化帧还必须满足路由 MTU。协议负责写入头部和 CRC，不必自行构造。

## 所有权

`xgl_send_zerocopy_at()` 返回后，调用者可以复用存储。即使使用 DMA，PHY TX 返回前也必须完成读取或复制。此接口减少组帧复制，驱动仍可能需要复制到自有 DMA 缓冲。

## 失败处理

检查返回值。不能传入只读存储、遗漏头部空间或保留回调指针。本地成功不能证明远端交付。调用和恢复须遵循与普通发送相同的串行化规则。
