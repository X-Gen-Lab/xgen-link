# SDK 4 迁移

SDK 4 直接从仓库根目录构建。清理旧构建缓存和安装前缀，然后按[构建与消费](build-and-consume.md)重新构建所有消费者，使用同一 profile 的库与生成头。

## 构建与公开 ABI

继续链接 `xgl::xgl`。以 CMake 导出和 `xgl_build_config.h` 作为能力与身份入口。旧源码装配和基础组件来源参数应从产品脚本移除。产品仍可为自己的其他库选择基础组件版本。

动态创建使用 XGL 公共头中的 `xgl_allocator_t`。按新描述符类型声明 `ctx`、`alloc`、`free`，回调成功时满足 `xgl_max_align_t` 对齐。适配旧分配器时建立一个新描述符并保持它及上下文活到销毁。使用类型匹配的回调和指针。

静态初始化仍从 `xgl_memory_requirements()` 获取实际需求。按新工具链和配置重新核对 RAM 容量、对齐与返回值。SDK 4 的 checked 入口校验配置大小、ABI 和构建身份。

## 收发行为更新

- 需要接收 DATA 的实例配置接纳回调。原来依靠空回调丢弃数据的产品改为显式接纳。纯发送端仍可使用空回调处理 ACK 和 CONTROL。
- 检查 `xgl_send_at()` 的同步分片硬错误和 `xgl_step*()` 的维护错误。遇到终态时，已交给 PHY 的片段仍可能存在于链路上。按[发送与恢复合同](send.md)处理旧 scope 和业务重试。
- Advanced 实际发送入口使用相同的 scope 期限判定。布局查询只用于准备空间，提交时重新判断期限。
- 核对可靠通信的[双向 MTU](../reference/protocol/v3/routing.md)。缺失本地反向路由或反馈预算不足在接纳前报错。
- 定制过编译帧长的生产者在配置库时设置 `-DXGL_DATALINK_MAX_FRAME_SIZE=<bytes>`，重新安装生成头和库，再重建消费者。

## 线上兼容

线上协议仍是 v3。两端都需支持所选 profile 所用的帧、扩展和认证策略。迁移步骤和测试见[互操作](../reference/protocol/v3/interoperability.md)及[验证](../development/validation.md)。

旧 SDK 3 装配方式保存在[历史迁移指南](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/docs/zh/guide/modular-migration.md)。
