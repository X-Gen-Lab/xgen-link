# 发送 API

使用 `xgl_send_at(handle, &tx, now_ms)`，时间源与 `xgl_step()` 的单调毫秒时间一致。返回成功表示本地接纳，不代表远端应用已完成处理。

## 数据所有权

调用期间借用 payload。可靠发送先将字节复制到有界的协议自有槽位，再保留用于重传；返回后调用者可以复用输入。PHY TX 必须同步消费或复制完整帧，不能保留协议指针。

```c
xgl_tx_data_t tx = {0};
tx.target_id = 2;
tx.data = payload;
tx.data_len = payload_len;
tx.reliable = true;
tx.connection_id = connection_id;
tx.session_epoch = session_epoch;
xgl_error_t result = xgl_send_at(handle, &tx, now_ms);
```

示例假定应用变量有效且 handle 已初始化。Payload 长度必须非零，priority 为 0–7。保留字段 `compression_id` 保持零。

## 可靠和非可靠发送

可靠发送同时受单 peer 窗口和全局 TX 容量约束。持续调用 `xgl_step()` 以接收 ACK、推进重传并报告最终失败。

非可靠发送不提供 ACK/重试保证。PHY 返回成功只确认本地帧接纳。SDK 不提供公开的逐消息交付完成回调，需要区分远端处理结果时应设计应用层响应。

## 分片

分片要求支持该能力的 profile，以及显式消息和重组预算。可靠消息的分片数可以超过窗口：每个 peer 保留一条消息，ACK 释放包槽后继续填充窗口。临时 PHY 或资源背压会暂停推进。不能因为尚未同步发出全部片段就重新提交消息。

可靠分片发送的 `XGL_OK` 表示消息已接纳；后续硬错误通过错误回调报告，该回调也可能发生在最初的调用中。最终失败会释放保留的数据，并将该 scope 标记为失败。

非可靠分片同步发送各片段，可能前面的片段已交给 PHY，后面才出错。重试整条应用消息不具备事务性。

## 接收接纳和回调

优先使用 `rx_accept_callback`：消费或复制 payload 后返回 `XGL_OK`，临时背压返回 `XGL_ERR_BUSY`。使用 `rx_callback` 则不能拒绝交付。

可靠包只有在应用接纳或进入协议自有的有界接收存储后才 ACK。因此完整分片消息可能已经 ACK，但仍在等待应用接纳；其字节继续保留并占用 RX 预算。需要继续 step 才能重试交付。

回调缓冲只在当前调用期间借用。回调不能重入、关闭或销毁实例，应把恢复动作排队，返回后执行。

## 失败和 Scope 恢复

按具体操作将 `XGL_ERR_WINDOW_FULL`、`XGL_ERR_BUSY` 和 `XGL_ERR_NO_MEMORY` 作为容量或背压处理；重试被拒绝的提交前先处理接收工作。其他错误应检查参数、路由、PHY 或编译能力。

可靠 scope 由远端 ID、connection ID 和 epoch 标识。已使用可靠包编号的 scope 不会因空闲超时被静默回收。硬失败或 RESET 不能导致序列号复用。未认证 scope 使用 `xgl_close_peer()` 关闭，排空旧链路流量后以新 epoch 重连；显式关闭时，尚有待发数据会报告 `XGL_ERR_CANCELLED`。认证 peer 则必须关闭安全会话并安装新的可信参数。

## 应用 Data Type

`data_type` 是应用分类字节，非零时增加 DATA_TYPE_EXT 并减少可用 payload。它与协议 DATA/ACK/控制包类型独立。选择 MTU 时要计入扩展、认证及 CRC 开销。
