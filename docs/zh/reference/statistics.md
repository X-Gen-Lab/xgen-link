# 统计

`xgl_statistics_t` 分别记录 datalink、network、transport 计数器，以及重传、CRC 错误和 RTT 指标。

## 解释

同一个包可能计入多个协议层，不能把各层字节数相加作为应用吞吐量。提交成功和 transport TX 计数不证明远端应用已经交付；应在接收回调测量已接纳的 payload。

## 访问

调用 `xgl_stats_get()` 或 `xgl_stats_reset()` 时，应用必须独占该实例。协议不提供内部 mutex 或原子快照保证。重置计数器不会重置 peer、包序号、会话或待处理数据。

## 编译配置

Full 和 Embedded 启用 `XGL_FEATURE_STATISTICS`。Boot 关闭统计对象及计数更新；有效实例上的 `xgl_stats_get()` / `xgl_stats_reset()` 返回 `XGL_ERR_UNSUPPORTED`，获取函数不改写输出。空指针和未初始化检查仍先执行。

## RTT 和内存

RTT 只统计未重传包的有效 ACK 样本，零时间差和超出有符号时间范围的样本不参与。平均值为整数毫秒；尚无样本时平均值和最大值为零，最小值为 `UINT32_MAX`。采样计数达到 `UINT32_MAX` 后停止累加观察值，协议自身的 RTO 估计仍独立更新。

`memory_used` 表示 `xgl_memory_requirements()` 为实例测得的完整 workspace 预留量，包含初始化区和各资源池；它不是载荷池当前活跃字节数。静态和分配器初始化都使用固定 workspace，因此 `memory_peak` 等于该预留量。重置统计保留这两个值，清空 RTT 观察值与各层计数；它不改变 RTT/RTO 控制状态。
