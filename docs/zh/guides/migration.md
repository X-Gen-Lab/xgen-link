---
id: guides/migration
type: how-to
audience: user
source_language: zh_CN
---

# 迁移既有集成

本次公开接口整理沿用 SDK 版本号 `0.0.1`。记录所用源码提交，使用新的构建目录和安装前缀，按[构建与消费](build-and-consume.md)重新编译所有消费者及其目标文件。配套使用同一快照、档位和资源组合的库与生成头，继续链接 `xgl::xgl`。

## 线上协议与应用

- **核对链路上端点和中继的 Wire v1 编码。** 本次公开接口整理保留现行 Wire v1 编码。仍在使用更早的 Wire v1 编码的产品须同步替换端点和中继，使帧标志位与 TLV 编号一致，见[线格式](../reference/protocol/v1/wire-format.md)。
- **移除旧 `XGL_TRAFFIC_ENCRYPTION_*` 常量的引用。** 流量类别只携带优先级和可靠性。
- **每次发送提交一个完整 DATA 帧。** 超出完整帧上限返回 `XGL_ERR_BUFFER_TOO_SMALL`。大数据改用[应用分块](send.md)，可参考文件传输与固件更新示例。接收 DATA 的实例需要配置接纳回调；纯发送端可使用空回调处理 ACK 和 CONTROL。
- **核对可靠通信的[双向 MTU](../reference/protocol/v1/routing.md)。** 分别检查 DATA 路径和 ACK 回程。缺失本地反向路由或反馈预算不足时，接收端在接纳前报错。

## 配置与存储

| 旧集成 | 当前入口 |
| --- | --- |
| `xgl_create*()` 后调用 `xgl_init()` | 包含 `<xgl/xgl_alloc.h>`，调用 `xgl_init_allocated(&config, allocator, &handle)`。成功后 handle 可立即使用；失败时输出清零。 |
| `xgl_config_validate()`、`xgl_init_static_with_options()`、`xgl_memory_report_with_options()` | 填写 `config.options`，再用 `xgl_memory_requirements()` 校验并测量、用 `xgl_init_static()` 初始化，或从 `<xgl/xgl_diagnostics.h>` 调用 `xgl_memory_report()`。 |
| 旧接收回调及 `options.rx_scoped_callback` | 将接收函数迁到 `config.rx_callback`，接收 `const xgl_rx_info_t*` 和载荷，消费或复制后返回 `XGL_OK`。接收上下文使用 `config.rx_user_data`；错误回调继续使用 `config.callback_user_data`。 |
| `xgl_next_timeout()` 与 `xgl_next_timeout_for_work()` | 从 `<xgl/xgl_runtime.h>` 使用 `xgl_deadline_query()`，第三个参数分别传 `NULL` 与下一次 step 的预算。 |
| `xgl_send_zerocopy_at()` 或嵌套的原地发送描述符 | 从 `<xgl/xgl_inplace.h>` 使用平铺字段的 `xgl_tx_zerocopy_request_t` 和 `xgl_send_zerocopy_scoped_at()`。默认 scope 的 connection ID 和 epoch 设为零。 |
| `xgl_version_string()` | 从 `<xgl/xgl_diagnostics.h>` 使用 `xgl_version_int()` 检查已链接版本。区分同为 `0.0.1` 的开发快照时，使用构建记录中的源码提交。 |

`XGL_INSTANCE_OPTIONS_INIT` 填写版本 5、描述符尺寸和配置尺寸。删除 `config.name`、`memory.allocator`、`features.enable_compression`、`features.enable_encryption`、路由 `metric`、发送 `compression_id` 及内存报告中的 `window_bytes`。通过新头和库重新测量存储及对齐。配置、回调上下文及显式 allocator 描述符均须存活到销毁结束。

如果集成仍使用更早的分片功能，移除分片开关、重组槽与超时、消息大小预算及 CMake `fragmentation` 组件请求。应用按独立 DATA 帧设计块格式。

定制编译帧长时，配置库的 `XGL_DATALINK_MAX_FRAME_SIZE`，然后重新安装生成头和库。发布检查见[互操作](../reference/protocol/v1/interoperability.md)与[验证](../development/validation.md)。

开发时期的[迁移记录](https://github.com/X-Gen-Lab/xgen-link/blob/d775c8a2fbc2dabcfc2406034e5cdc43808954bd/docs/zh/guides/migration.md)和[装配指南](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/docs/zh/guide/modular-migration.md)保存在各自的固定提交。
