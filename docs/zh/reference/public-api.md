# 公开 API

使用 `<xgl/xgl.h>`。安装头文件包含生成的 profile 和 ABI 常量；`internal/` 头文件不安装。

## 生命周期与所有权

`xgl_memory_requirements()` 测量精确存储需求。`xgl_init_static()` 初始化对齐的调用者存储。`xgl_create()` 通过 `config.memory.allocator`（`xgm_allocator_t`）预留同一布局，随后需要调用 `xgl_init()`。只有 Full 可以选择可选的 xgen-memory libc 后端。`xgl_destroy()` 释放动态预留的 workspace，或结束对静态存储的使用。

配置及其引用的 PHY、provider、回调对象借用到销毁。实例存活期间必须保持地址稳定且不可变。同一实例的操作由调用者串行化；回调不得重入实例。

## 发送与调度

`xgl_send_at(handle, &tx, now_ms)` 在调用期间借用 payload，并将可靠数据保留在有界协议存储中。`xgl_send_zerocopy_at()` 使用调用者帧存储进行非可靠单帧发送，不允许异步 PHY 保留该地址。

`xgl_step(handle, now_ms, &budget)` 轮询到期链路并执行 transport 维护。`budget.rx_bytes` 是每条链路的字节上限，不是频率。`xgl_next_timeout(handle, now_ms, &delay)` 返回是否存在相对截止时间。所有调用使用同一按模 2^32 回绕的单调毫秒时钟；经过的时间间隔必须小于 2^31。

## 会话与诊断

`xgl_install_security_session()` 复制显式可信的方向性参数。`xgl_close_security_session()` 关闭关联、释放匹配的 transport peer，并保留安全墓碑。`xgl_close_peer()` 取消未认证 scope；重新连接前须排空旧流量并使用新 epoch。

`xgl_stats_get()` / `xgl_stats_reset()` 要求独占实例访问。`xgl_error_string()` 解释协议错误域。`xgl_version_string()` / `xgl_version_int()` 报告已链接 SDK 的版本。

参见[迁移指南](../guide/modular-migration.md)与[安全](../protocol/security.md)。
