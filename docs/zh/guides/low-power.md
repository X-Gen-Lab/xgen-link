---
id: guides/low-power
type: how-to
audience: user
source_language: zh_CN
---

# 低功耗运行时

应用显式驱动协议工作。协议不内置时钟 provider、定时器线程或 sleep 实现。

## 相对超时 API

`xgl_step(handle, now_ms, &budget)` 轮询到期链路并推进 transport。`budget.rx_bytes` 是每条链路的字节上限；为零时跳过 RX 轮询，但仍维护 parser 超时和 transport。`receive_timeout_ms` 控制不完整帧的 parser 超时，不是 API 阻塞等待时间。零表示关闭 parser 超时；最近一次有效 step 的超时策略应用于所有半帧，仍从原始起始时间计算，修改策略不会重新计时。

Parser 截止时间独立于 PHY 轮询间隔参与 `xgl_deadline_query()`。即使 PHY 每秒才轮询一次，10 ms 的半帧仍会在 10 ms 到期。到期后只重置和报告一次，零 RX 字节预算也会维护超时。

Step 返回最先出现的 parser 超时、PHY、交付或 transport 错误，同时继续消费已读出的整个字节批次并维护 transport。流中的格式错误帧被丢弃并通过诊断报告，不阻断后续帧的解析。PHY 成功读出字节不等于应用成功接纳。

ACK/SACK 接收负责校验反馈并记录可执行工作。DATA 重传在 RX 处理后的 transport 维护阶段执行，可能仍在同一次 step 内完成。就绪的重传可以给出零超时；临时 PHY 或资源背压安排正的重试延迟。维护返回首个硬错误并继续其他 scope；重试已保留工作时的 BUSY 或 NO_MEMORY 属于正常背压，不会单独使 step 失败。即时 RX 接纳和 ACK 提交错误仍向上传递，ACK 失败不撤销已接纳字节或接收历史。

每条到期链路在一次 step 中只读取 PHY 一次，读取量还受构建时 `XGL_DATALINK_RX_CHUNK_SIZE` 限制。`AUTO` 在 Boot 为 32 字节，其余 profile 为 128 字节；运行期预算不是要求把该预算全部读满。选择较小 chunk 时，应重新核对整帧所需的轮询次数、驱动缓冲容量及 parser 超时，详见 [Datalink](../explanation/datalink.md)。

在 `<xgl/xgl_runtime.h>` 中，`xgl_deadline_query(handle, now_ms, budget, &delay_ms)` 在存在截止事件时返回 true。延迟为零表示立即有工作；false 保留输出原值，调用方须分开处理。

预算指针为 `NULL` 时，查询包含全部 PHY 轮询，并使用最近一次有效 step 的 parser 超时策略。传入预算时，使用该预算的 `receive_timeout_ms`；`rx_bytes` 为零排除 PHY 轮询，仍计算 transport 和适用的半帧 deadline。查询不推进工作、不改变策略或重启计时。预算超时须小于 `2^31` 毫秒。无效输入返回 false 并保留输出。禁用 RX 后如果没有协议截止事件，应用事件需再次唤醒 owner。

## 主循环集成

应用循环示意：

```c
const xgl_work_budget_t budget = {128U, 100U};
for (;;) {
    uint32_t now = board_monotonic_ms();
    xgl_error_t result = xgl_step(handle, now, &budget);
    app_handle_step_result(result);
    uint32_t delay;
    bool timed = xgl_deadline_query(
        handle, board_monotonic_ms(), &budget, &delay);
    app_wait_for_rx_or_timeout(timed, timed ? delay : 0U);
}
```

其中 board/application 函数是集成占位符，不是 SDK API。事件注册与等待须避免竞态，不能在轮询和睡眠之间丢失 RX 通知。唤醒后重新读取时间。

路由轮询截止时间也包含在 timeout 结果中。如果配置的链路轮询间隔尚未到，RX 事件仍可能要等后续调度才被读取。

## RTOS 集成

在 owner task 中运行同样的循环，等待事件或相对超时。ISR 只收集字节并通知任务。同一实例不能存在重叠的协议调用。

回调执行时间应有上限。RX 字节预算不能限制用户回调耗时，也不能限制全部 transport 维护工作，应测量完整 step 的最坏执行时间。

## 时间和恢复

统一使用单调的 32 位毫秒时钟，经过时间小于 `2^31` 毫秒。支持时钟回绕，不支持在实例仍运行时重置时钟作为恢复手段。

存在在途包、待推进消息或应用 BUSY 待交付数据时，继续调度。正确处理背压和 scope 最终失败；发送失败后无限睡眠不会释放保留资源。

应用 BUSY 的接收数据不会创建持续就绪的定时事件。应用恢复接纳能力后，应通知 owner task 并驱动一次 step。
