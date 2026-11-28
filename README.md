# XGen Link

XGen Link 是面向嵌入式设备的 C11 通信协议库。它在产品提供的物理链路上完成组帧、路由和可靠单播。产品提供 PHY 收发、单调时钟和执行上下文，并按需要接入认证服务。应用显式填写每个实例的节点地址、MTU、超时、窗口和收发容量，校验后可使用调用方提供的静态工作区。[配置与 C11 示例](docs/zh/guides/configuration.md)

## 构建并安装 SDK

准备 C11 编译器和 CMake 3.24 或更高版本。在仓库根目录执行以下命令，生成静态库、匹配的公开配置头和 CMake 包。安装目录使用相对于当前目录的路径，可直接在 PowerShell 或 Bash 中运行。

```sh
cmake -S . -B build/production -DCMAKE_BUILD_TYPE=Release
cmake --build build/production --config Release
cmake --install build/production --config Release --prefix build/sdk
```

在应用工程中，将安装前缀的绝对路径传给 `CMAKE_PREFIX_PATH`，然后链接导出目标。以下示例假定应用已定义目标 `my_application`。

```cmake
find_package(xgl 0.0.1 CONFIG REQUIRED COMPONENTS wire reliable)
target_link_libraries(my_application PRIVATE xgl::xgl)
```

应用也可用 `add_subdirectory()` 集成源码并链接同一目标。不同工具链、档位或资源配置使用各自的构建目录和安装前缀，库与生成头配套使用。[构建与消费](docs/zh/guides/build-and-consume.md)

静态集成使用 `<xgl/xgl.h>` 完成工作区测量、初始化、收发、关闭和销毁。按需包含分配初始化、原地发送、运行调度、安全或诊断扩展头。[公开 API](docs/zh/reference/api.md)

## 选择档位

| 档位 | 编译能力 |
| --- | --- |
| `boot` | 可靠单播、静态工作区。适合链路更新或小型加载程序。 |
| `embedded-lite` | 在受限资源下提供乱序接收。 |
| `embedded` | 支持乱序、路由转发和可选认证。 |
| `full` | 支持完整协议能力，默认认证会话容量更大。 |

在 CMake 配置阶段用 `XGL_PROFILE` 选择档位。窗口、帧上限和读取块大小等选项会影响生成库的资源身份。每次 DATA 提交必须装入一帧，更大的业务数据由应用分块。[档位与容量](docs/zh/reference/profiles.md) · [Wire v1](docs/zh/reference/protocol/v1/index.md)

## 从哪里开始

| 任务 | 入口 |
| --- | --- |
| 跑通第一次收发，观察发送、接收与轮询 | [首次收发](docs/zh/tutorials/first-link.md) |
| 接入串口等 PHY，约定缓冲区借用和执行上下文 | [移植指南](docs/zh/guides/porting.md) · [公开 API](docs/zh/reference/api.md) |
| 接入 Boot 传输，估算工作区和目标资源 | [Boot 接入](docs/zh/guides/boot.md) · [资源模型](docs/zh/explanation/resource-model.md) |
| 升级 SDK，核对端点及中继的兼容性 | [迁移指南](docs/zh/guides/migration.md) · [变更记录](CHANGELOG.md) |
| 修改协议或构建工具，执行主机与目标验证 | [贡献指南](CONTRIBUTING.md) · [工程约定](docs/standards.md) |

完整导航见[中文文档](docs/zh/index.md)和[英文站点](https://x-gen-lab.github.io/xgen-link/en/)。
