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

示例假定应用变量有效且 handle 已初始化。Payload 长度必须非零，`priority` 为 0–7，只编码到 wire traffic class；SDK 不据此排序本地发送或抢占其他 peer。保留字段 `compression_id` 保持零。

## 调用者提供帧存储

`xgl_send_zerocopy_at()` 保持原请求结构布局，始终使用零 connection ID 和零 epoch。显式 scope 使用 `xgl_tx_zerocopy_request_t` 与 `xgl_send_zerocopy_scoped_at()`。两个入口都只支持非空、非可靠、单帧 payload；`reliable=true` 会被拒绝，`timeout_ms` 被忽略。它们不提供保留缓冲区的异步 DMA 或重传所有权。

放置 payload 前先调用 `xgl_send_zerocopy_layout()`。查询忽略 `data.buffer`、`data.buffer_size` 和 `data.data_offset`，验证发送元数据、路由、完整帧 MTU、已安装的认证 scope，以及终止 scope 策略。输出 `payload_offset` 与 `frame_size`，包含 DATA_TYPE、非零 SESSION、SECURITY、固定标签及末尾 CRC。查询不分配内存、不修改计数或安全序号、不调用 provider 或 PHY。查询失败会清零输出；查询成功不预留资源，发送时仍会验证当前状态。

```c
xgl_tx_zerocopy_request_t request = {0};
request.data.target_id = 2;
request.data.data_type = 9;
request.data.data_len = payload_len;
request.connection_id = connection_id;
request.session_epoch = session_epoch;
xgl_tx_frame_layout_t layout;
xgl_error_t result = xgl_send_zerocopy_layout(handle, &request, &layout);
if (result == XGL_OK && layout.frame_size <= sizeof(frame_buffer)) {
    request.data.buffer = frame_buffer;
    request.data.buffer_size = sizeof(frame_buffer);
    request.data.data_offset = layout.payload_offset;
    memcpy(frame_buffer + layout.payload_offset, payload, payload_len);
    result = xgl_send_zerocopy_scoped_at(handle, &request, now_ms);
}
```

示例假定存在调用者拥有的数组 `frame_buffer`，并包含 `<string.h>`。数组容量不足时应报告容量错误，不提交发送。发送保持 payload 原位；帧头和尾部可写，即使认证或 PHY 提交失败也可能已经改变。所有字节只借用到调用返回。

Network TX 包数及 payload 字节数只在下层提交成功后计入，普通、原地与转发路径保持一致。这些计数不证明远端交付。

Boot 私有的 network 逻辑发送工作区最多容纳 32 字节扩展，足够同时组合一个 ACK_RANGE、SESSION 和 DATA_TYPE。更长的逻辑发送扩展列表会在提交 PHY 前被拒绝。该 profile 限制不改变 Wire v1：完整帧编解码仍支持 255 字节帧头，其他 profile 保留完整的 231 字节逻辑扩展工作区。

## 可靠和非可靠发送

可靠发送同时受单 peer 窗口和全局 TX 容量约束。持续调用 `xgl_step()` 以接收 ACK、推进重传并报告最终失败。

普通发送的 `timeout_ms` 指定每个可靠包的初始 ACK 等待时间，应用事务有独立的完成期限。零值使用当前 profile 支持的 RTT 估计，尚无估计时使用配置的 ACK 等待时间，配置值也为零则回退到内建默认值；重传可调整等待时间。非可靠普通发送不创建 ACK 计时器，两个零拷贝发送入口及布局查询都忽略该字段。

`reliable=true` 要求单播目标。广播目标 `0xFFFF` 会在分配 peer 或重传状态前以 `XGL_ERR_INVALID_PARAM` 拒绝。认证策略允许时，普通非可靠广播仍可通过显式路由发送。

非可靠发送不提供 ACK/重试保证。PHY 返回成功只确认本地帧接纳。SDK 不提供公开的逐消息交付完成回调，需要区分远端处理结果时应设计应用层响应。

## 应用分块

每次 `xgl_send_at()` 提交一个 DATA 帧。按[线格式](../reference/protocol/v1/wire-format.md)计算载荷上限，计入完整路径 MTU、实际 TLV 和认证标签。超出一帧返回 `XGL_ERR_BUFFER_TOO_SMALL`，发生在创建 peer、发送 HELLO 和提交 PHY 之前。

传输文件或固件时，应用定义块编号与写入位置，并让每个块装入一帧。可靠传输处理单块的丢包与重试，业务回执可以确认整批数据是否写入或生效。[可运行示例](../tutorials/examples.md)包含文件分块和固件写入确认。

## 接收接纳和回调

优先使用 `rx_accept_callback`：消费或复制 payload 后返回 `XGL_OK`，临时背压返回 `XGL_ERR_BUSY`。使用 `rx_callback` 则不能拒绝交付。

本地 DATA 需要配置接收回调。Advanced 的 scoped 回调优先，其次是 accept 和 legacy 回调。全部缺失时，`xgl_step*()` 对本地 DATA 返回 `XGL_ERR_UNSUPPORTED`，DATA 不进入缓存或确认。纯发送端可以保持空的接收回调并处理 ACK 和 CONTROL。有意丢弃数据的产品应配置显式接纳回调。

顺序包在应用接纳后才发送 ACK；乱序包只有进入协议自有的有界缓存才可发送 SACK。若已缓存的包暂时无法交付，继续运行 step 可重新尝试。

ACK/SACK 提交错误会返回，但不撤销已接纳字节或接收编号；重试相同入站 DATA 不会重复已经成功的应用交付。后续维护遇到 BUSY 或 NO_MEMORY 时保留自有数据，作为正常背压处理；应用硬错误则终止所属 scope，释放字节并只报告一次。应用容量恢复后应通知协议 owner，因为仅等待应用交付不会创建就绪超时。

回调缓冲只在当前调用期间借用。回调不能重入、关闭或销毁实例，应把恢复动作排队，返回后执行。

现有 `rx_callback` 和 `rx_accept_callback` 只提供发送端 ID、应用 data type 和 payload，不提供 connection ID、epoch 或包编号。`error_callback` 只提供实例、错误码和诊断文本，不提供失败 scope 或逐消息标识；诊断文本还可能为空。SDK 也没有逐消息发送完成回调，应用不能仅从这些回调区分同一远端的多个 scope 或确认某条消息已被远端处理。需要这种关联时应由应用协议携带标识并定义响应。

## 失败和 Scope 恢复

按具体操作将 `XGL_ERR_WINDOW_FULL`、`XGL_ERR_BUSY` 和 `XGL_ERR_NO_MEMORY` 作为容量或背压处理；重试被拒绝的提交前先处理接收工作。其他错误应检查参数、路由、PHY 或编译能力。

`xgl_step()` 返回首个接收错误或维护硬错误，同时继续独立工作。维护重试在临时背压后仍持有数据时，应用无需再次提交该数据。错误回调描述 scope 失败和指定诊断，不能替代检查操作返回值。

可靠 scope 由远端 ID、connection ID 和 epoch 标识。现存 scope 终止后，同一精确 scope 的全部 DATA TX 都会拒绝，包括普通可靠或非可靠发送、两个零拷贝发送入口；纯布局查询也会拒绝，有效请求返回 `XGL_ERR_ACK_TIMEOUT`。其他 connection ID 或 epoch 不受该终止状态影响。拒绝不创建 peer、不调用认证 provider 或 PHY，也不增加发送尝试统计。

已使用可靠包编号的 scope 不会因空闲超时被静默回收。硬失败或 RESET 不能导致序列号复用。未认证 scope 使用 `xgl_close_peer()` 关闭，排空旧链路流量后以新 epoch 重连；显式关闭时，尚有待发数据会报告 `XGL_ERR_CANCELLED`。认证 peer 则必须关闭安全会话并安装新的可信参数。

## 应用 Data Type

`data_type` 是应用分类字节，非零时增加 DATA_TYPE_EXT 并减少可用 payload。它与协议 DATA/ACK/控制包类型独立。选择 MTU 时要计入扩展、认证及 CRC 开销。
