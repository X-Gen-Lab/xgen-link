---
id: reference/api
type: reference
audience: user
source_language: zh_CN
---

# 公开 API

[生成的 C API 参考](c-api.md)。

应用以 `<xgl/xgl.h>` 开始。安装包包含配套的档位和资源配置头，以及以下按功能选择的公开扩展头。

## 生命周期与所有权

`xgl_memory_requirements()` 校验配置并测量精确存储需求。`xgl_init_static()` 用对齐的调用者存储初始化实例，成功后可立即使用。`xgl_destroy()` 结束对静态存储的借用，或通过原分配后端释放拥有的工作区。配置、选项、PHY、provider 和回调对象借用到销毁，地址保持稳定。调用方串行化同一实例的操作，回调不得重入实例。

## 发送与调度

`xgl_send_at(handle, &tx, now_ms)` 每次提交一个 DATA 帧，可靠载荷复制到有界协议存储。`xgl_step(handle, now_ms, &budget)` 轮询到期链路并维护 transport。`budget.rx_bytes` 是每条链路的新输入字节上限。所有调用使用同一按模 `2^32` 回绕的单调毫秒时钟，经过的时间间隔须小于 `2^31`。`xgl_close_peer()` 取消未认证 scope，应用排空旧流量并以新 epoch 重连。

`config.rx_callback` 在本机 DATA 交付时接收 `xgl_rx_info_t` 和借用的载荷，消费或复制后返回 `XGL_OK`。接收回调使用 `config.rx_user_data`，错误回调使用独立的 `config.callback_user_data`。详见[接收接纳](../guides/send.md)。

## 扩展头

| 公开头 | 入口 |
| --- | --- |
| `<xgl/xgl_alloc.h>` | `xgl_init_allocated()` 一次完成工作区申请和初始化，成功后实例可立即使用。传入显式 allocator，或传入 `NULL` 采用编译时 libc 策略。Full 默认开启回退，Embedded/Lite 默认关闭，Boot 禁止回退。 |
| `<xgl/xgl_inplace.h>` | `xgl_send_zerocopy_layout()` 查询精确头部空间和总容量；`xgl_send_zerocopy_scoped_at()` 在调用者存储中同步提交非可靠单帧。见[原地发送](../guides/in-place-send.md)。 |
| `<xgl/xgl_runtime.h>` | `xgl_deadline_query()` 查询相对截止时间，可传下一次 step 的预算。`xgl_step_bounded()` 与 `config.options` 中的运行策略由高级运行时构建能力提供。见[低功耗运行时](../guides/low-power.md)与[扩展运行选项](../guides/advanced-runtime.md)。 |
| `<xgl/xgl_security.h>` | 安装或关闭可信的方向性关联。关闭时释放匹配的 transport peer，并保留 nonce 域的占用记录。见[安全](protocol/v1/security.md)。 |
| `<xgl/xgl_diagnostics.h>` | `xgl_memory_report()` 解释各资源预留，`xgl_resources_get()` 查询当前占用，`xgl_stats_get()` 与 `xgl_stats_reset()` 访问计数；`xgl_error_string()` 解释状态码，`xgl_version_int()` 报告链接版本。见[资源模型](../explanation/resource-model.md)与[统计](statistics.md)。 |

参见[迁移指南](../guides/migration.md)与[安全](protocol/v1/security.md)。
