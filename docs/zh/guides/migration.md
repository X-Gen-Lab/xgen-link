---
id: guides/migration
type: how-to
audience: user
source_language: zh_CN
---

# 迁移既有集成

将此前开发快照中的集成迁移到 SDK 0.0.1 时，使用新的构建目录和安装前缀。配置 ABI 为 `5`，线上协议为 Wire v1。按[构建与消费](build-and-consume.md)重新构建产品，配套使用同一档位和资源组合的库与生成头，继续链接 `xgl::xgl`。

## 线上协议与应用

- **核对链路上端点和中继的 Wire v1 编码。** 版本字段为 `1`。迁移使用早期 Wire v1 编码的产品时，同步替换端点和中继，使帧标志位和 TLV 编号一致。基本帧头布局及 CRC 算法见[线格式](../reference/protocol/v1/wire-format.md)。
- **移除旧 `XGL_TRAFFIC_ENCRYPTION_*` 常量的引用。** 流量类别只携带优先级和可靠性。
- **每次发送提交一个完整 DATA 帧。** 超出完整帧上限返回 `XGL_ERR_BUFFER_TOO_SMALL`。大数据改用[应用分块](send.md)，可参考文件传输与固件更新示例。接收 DATA 的实例需要配置接纳回调；纯发送端可使用空回调处理 ACK 和 CONTROL。
- **核对可靠通信的[双向 MTU](../reference/protocol/v1/routing.md)。** 分别检查 DATA 路径和 ACK 回程。缺失本地反向路由或反馈预算不足时，接收端在接纳前报错。

## 配置与存储

移除代码中的分片开关、整消息字节预算和重组槽配置，同时清理 CMake 的 `fragmentation` 组件请求，以及使用旧消息槽选项、重组超时、内存报告或资源快照字段的代码。`XGL_INSTANCE_OPTIONS_INIT` 初始化版本 2 的运行选项描述符，填写尺寸、版本和构建身份。带 scope 的回调、已安装 scope 的认证策略和可靠发送忙期选项继续可用。

通过 `xgl_memory_requirements()` 或 `xgl_memory_report_with_options()` 重新测量实际所需存储及对齐。配置和回调上下文须活到销毁结束。动态创建继续使用公开的 `xgl_allocator_t`，分配器描述符和上下文也须活到销毁结束。Checked 入口校验配置大小、ABI 和构建身份。

定制编译帧长时，配置库的 `XGL_DATALINK_MAX_FRAME_SIZE`，然后重新安装生成头和库。发布检查见[互操作](../reference/protocol/v1/interoperability.md)与[验证](../development/validation.md)。

开发时期的[迁移记录](https://github.com/X-Gen-Lab/xgen-link/blob/d775c8a2fbc2dabcfc2406034e5cdc43808954bd/docs/zh/guides/migration.md)和[装配指南](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/docs/zh/guide/modular-migration.md)保存在各自的固定提交。
