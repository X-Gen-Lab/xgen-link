# 统计

`xgl_statistics_t` 分别记录 datalink、network、transport 计数器，以及重传、CRC 错误和 RTT 指标。

## 解释

同一个包可能计入多个协议层，不能把各层字节数相加作为应用吞吐量。提交成功和 transport TX 计数不证明远端应用已经交付；应在接收回调测量已接纳的 payload。

## 访问

调用 `xgl_stats_get()` 或 `xgl_stats_reset()` 时，应用必须独占该实例。协议不提供内部 mutex 或原子快照保证。重置计数器不会重置 peer、包序号、会话或待处理数据。
