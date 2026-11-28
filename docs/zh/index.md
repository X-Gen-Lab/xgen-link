---
id: index
type: landing
audience: user
source_language: zh_CN
---

# XGen Link SDK

XGen Link 为嵌入式系统提供 C11 通信协议、显式有界存储与可靠传输。产品提供 PHY、时间和执行者。

| 任务 | 入口 |
| --- | --- |
| 第一次运行 | [跑通收发](tutorials/first-link.md) |
| 构建、安装和接入工程 | [构建与消费](guides/build-and-consume.md) |
| 配置容量和硬件接口 | [配置](guides/configuration.md)、[移植](guides/porting.md) |
| 接入 Boot 传输 | [Boot 接入](guides/boot.md) |
| 了解架构及资源 | [架构](explanation/architecture.md)、[资源模型](explanation/resource-model.md) |
| 查 API 与协议 | [公开 API](reference/api.md)、[Wire v1](reference/protocol/v1/index.md) |
| 开发和验证 | [开发工作流](development/workflow.md)、[验证](development/validation.md) |


```{toctree}
:maxdepth: 2

tutorials/first-link
tutorials/examples
guides/build-and-consume
guides/configuration
guides/porting
guides/send
guides/in-place-send
guides/boot
guides/low-power
guides/advanced-runtime
guides/migration
guides/troubleshooting
explanation/architecture
explanation/resource-model
explanation/runtime
explanation/datalink
explanation/transport-internals
explanation/state-machines
explanation/implementation-map
reference/api
reference/profiles
reference/errors
reference/statistics
reference/glossary
reference/protocol/v1/index
reference/protocol/v1/wire-format
reference/protocol/v1/extensions
reference/protocol/v1/reliability
reference/protocol/v1/routing
reference/protocol/v1/security
reference/protocol/v1/interoperability
development/workflow
development/validation
development/documentation
reference/c-api
```
