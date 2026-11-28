# 变更记录

## 0.0.1

### 通信协议

- 提供 Wire v1 编解码、CRC 校验、流式组帧和可靠单播。每次 DATA 提交对应一帧，超过帧容量的数据由应用分块。[Wire v1 参考](docs/zh/reference/protocol/v1/index.md)
- 提供 `boot`、`embedded-lite`、`embedded` 和 `full` 四种编译档位。产品可以按目标选择静态工作区、乱序接收、转发及认证能力。[档位与资源](docs/zh/reference/profiles.md)

### SDK 与集成

- SDK 版本标识为 `0.0.1`，`xgl_version_int()` 返回 `1`。`<xgl/xgl.h>` 聚焦静态初始化、发送、推进、关闭和销毁，分配初始化、原地发送、运行调度、认证和诊断各有公开扩展头。工作区测量校验 `xgl_config_t`，两类初始化成功后均返回可用实例。[公开 API](docs/zh/reference/api.md)
- 统一使用携带连接与 epoch 的接收回调，并为接收和错误回调分别配置上下文。高级运行策略通过 `config.options` 传入版本 5 描述符。升级旧快照时，使用新快照中的配套库和头文件重新编译全部消费者。此次 API 整理沿用现行 Wire v1。[迁移指南](docs/zh/guides/migration.md)
- C11 静态库支持源码接入和安装包消费，两种方式都链接 `xgl::xgl`。安装包导出配套的公开头、档位、能力和资源身份。[构建与消费](docs/zh/guides/build-and-consume.md)

### 开发与验证

- 提供锁定的维护工具、CMake preset 和 Git 提交检查，覆盖主机 CTest、SDK 独立消费、Wire 互通和 Cortex-M0 仿真。[开发工作流](docs/zh/development/workflow.md) · [验收说明](docs/zh/development/validation.md)
- 中文文档与英文译文通过 Sphinx 构建，公开 C API 从头文件生成。[文档入口](docs/zh/index.md)
