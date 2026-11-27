# 公开 API

[生成的 C API 参考](../../api/index.md)。

使用 `<xgl/xgl.h>`。安装头文件包含生成的 profile 和 ABI 常量；`internal/` 头文件不安装。

## 生命周期与所有权

`xgl_memory_requirements()` 测量精确存储需求。`xgl_init_static()` 初始化对齐的调用者存储。`xgl_create()` 通过 `config.memory.allocator`（`xgl_allocator_t`）预留同一布局，随后需要调用 `xgl_init()`。Libc fallback 默认为 Full 开启、Embedded/Lite 关闭；Embedded/Lite 可在重新构建 SDK 时显式启用，Boot 禁止 fallback。调用者显式提供的 allocator 可以使用静态存储或堆，因此仅关闭协议 fallback 不等于产品整体无堆。`xgl_destroy()` 释放动态预留的 workspace，或结束对静态存储的使用。

需要诊断创建失败时使用 `xgl_create_with_error(&config, &handle)`：返回配置、ABI、后端或分配错误；分配失败为 `XGL_ERR_NO_MEMORY`，失败时清空 handle。成功仍只预留 workspace，需要调用 `xgl_init()`。原 `xgl_create()` 的指针返回形式保持兼容，复用同一实现。跨编译边界可使用对应的 `_checked` 入口显式传入消费者配置大小、ABI 和构建身份。

`xgl_memory_report()` 返回同一份需求及按资源类别划分的字节明细，不分配、不修改实例。类别包含对齐填充，合计为精确 workspace 大小。接纳字节预算可能小于池的实际预留，见[资源模型](../explanation/resource-model.md)。

配置及其引用的 PHY、provider、回调对象借用到销毁。实例存活期间必须保持地址稳定且不可变。同一实例的操作由调用者串行化；回调不得重入实例。

## 发送与调度

`xgl_send_at(handle, &tx, now_ms)` 每次提交发送一个 DATA 帧，在调用期间借用 payload，并将可靠数据保留在有界协议存储中。`xgl_send_zerocopy_at()` 使用调用者帧存储进行非可靠单帧发送，不允许异步 PHY 保留该地址。

原零拷贝请求始终使用零 connection/epoch。`xgl_send_zerocopy_scoped_at()` 接受独立且兼容的显式 scope 请求；`xgl_send_zerocopy_layout()` 查询精确头部空间和总容量，不分配、不发送、不消耗认证状态。准备与生命周期规则见[发送 API](../guides/send.md)。

`xgl_step(handle, now_ms, &budget)` 轮询到期链路并执行 transport 维护。`budget.rx_bytes` 是每条链路的字节上限，不是频率。`xgl_next_timeout(handle, now_ms, &delay)` 返回是否存在相对截止时间。所有调用使用同一按模 2^32 回绕的单调毫秒时钟；经过的时间间隔必须小于 2^31。

`xgl_next_timeout_for_work(handle, now_ms, &budget, &delay)` 按下一次 step 的预算查询截止时间。RX 字节为零时排除 PHY 轮询，仍包括 transport 和该预算启用的半帧超时；查询不改变实例或最近 step 的超时策略。旧查询仍包含所有 PHY 轮询。两种查询返回 false 均不修改输出，详见[低功耗运行时](../guides/low-power.md)。

## 会话与诊断

`xgl_install_security_session()` 复制显式可信的方向性参数。`xgl_close_security_session()` 关闭关联、释放匹配的 transport peer，并保留安全墓碑。`xgl_close_peer()` 取消未认证 scope；重新连接前须排空旧流量并使用新 epoch。

`xgl_stats_get()` / `xgl_stats_reset()` 要求独占实例访问。`xgl_error_string()` 解释协议错误域。`xgl_version_string()` / `xgl_version_int()` 报告已链接 SDK 的版本。

参见[迁移指南](../guides/migration.md)与[安全](protocol/v1/security.md)。
