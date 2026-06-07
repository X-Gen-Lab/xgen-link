# 低功耗运行时

协议处理由应用周期性调用 `xgl_run()` 驱动。

## Deadline API

`xgl_next_deadline_ms(handle)` 返回下一次协议处理 deadline。没有 route polling、重传或重组 deadline 时返回 `XGL_NO_DEADLINE_MS`。

## Bare-Metal 模型

```c
while (1) {
    xgl_run(handle, 100);
    uint32_t wait_ms = xgl_next_deadline_ms(handle);
    sleep_until_rx_or_timeout(wait_ms);
}
```

## RTOS 模型

在 task 中运行协议。PHY ISR 只写 ring buffer 或发信号，不直接调用 parser、auth 或 transport。
