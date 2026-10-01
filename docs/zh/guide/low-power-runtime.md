# 低功耗运行时

应用显式驱动协议工作。协议不内置时钟 provider、定时器线程或 sleep 实现。

## 相对超时 API

`xgl_step(handle, now_ms, &budget)` 轮询到期链路并推进 transport。`budget.rx_bytes` 是每条链路的字节上限；为零时跳过 RX 轮询，但仍维护 transport。`receive_timeout_ms` 控制不完整帧的 parser 超时，不是 API 阻塞等待时间。

`xgl_next_timeout(handle, now_ms, &delay_ms)` 在存在截止事件时返回 true。延迟为零表示立即有工作；false 不修改输出，必须单独处理，不能按未初始化的值睡眠。

## 主循环集成

应用循环示意：

```c
const xgl_work_budget_t budget = {128U, 100U};
for (;;) {
    uint32_t now = board_monotonic_ms();
    xgl_error_t result = xgl_step(handle, now, &budget);
    app_handle_step_result(result);
    uint32_t delay;
    bool timed = xgl_next_timeout(handle, board_monotonic_ms(), &delay);
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
