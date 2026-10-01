# xgen-link

[English documentation](docs/en/index.md) · [中文文档](docs/zh/index.md) · [实施状态](REFACTORING_STATUS.md)

Portable C11 datalink, wire, network, transport and security layers for bounded embedded systems. Generic capabilities belong to the independent xgen-memory, xgen-containers, xgen-bytes, xgen-crc and xgen-status packages. Each package is consumed through its public targets, with version and ABI checks.

面向有界嵌入式系统的 C11 协议栈。通用能力由独立组件提供，协议仓库不保留其实现；静态初始化与 allocator 初始化使用同一工作区布局、同一状态机。

## Build / 构建

```sh
cmake --preset gcc-test \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset gcc-test
ctest --preset gcc-test
```

先显式准备 GoogleTest 1.16.0（包含 GoogleMock）；配置过程不下载依赖。无 libc 的 Boot 构建，把预设替换为 `boot`，无需 GoogleTest；需要有界分片、认证和转发时使用 `embedded`。默认 `full` 面向主机。生产库为 C11，仅主机测试需要 C++。

先将 status、bytes、CRC、memory、containers 五包安装到示例 SDK 前缀。正式模块只消费父工程预提供的兼容 targets 或已安装 package，检查版本 `>=0.1.0,<0.2.0`、ABI 1、target 类型及包内版本一致性；产品负责选择同一镜像的唯一依赖版本。link 不再保留 `external` 生产子模块，也不会递归获取组件。

独立源码开发使用 [dev 装配入口](dev/README.md)，通过五个 `XGL_DEV_*_SOURCE_DIR` 显式提供已准备的仓库。旧 `XGL_*_SOURCE_DIR` 生产路径已移除并报错；不要只改变量名后继续配置根目录。`dev/dependencies.json` 固定本仓开发/CI 测试输入，不决定产品版本。作为产品子目录接入时，示例、smoke 与发布辅助默认关闭，需要时显式开启。实际验证及远端状态见[实施状态](REFACTORING_STATUS.md)。

The production module accepts compatible parent-provided targets or installed packages only. Products own dependency selection; standalone source development uses the explicit [dev harness](dev/README.md). No production dependency is selected through nested submodules or source-directory fallbacks.

## Architecture / 架构

Application → API → Transport → Network → Datalink → synchronous PHY。Wire 负责统一编码，Security 管理显式可信会话；通用算法调用各独立组件。

- `xgl_memory_requirements` / `xgl_init_static`：精确有界存储，无运行期堆回退。
- `xgl_send_at` / `xgl_step` / `xgl_next_timeout`：调用者提供时间。
- 配置和引用对象借用到销毁；同一实例由应用串行访问，回调不可重入。
- wire v3 分离可靠 DATA 编号与 64 位安全序号，不降级 v2。认证需要可信方向参数及生产密码 provider。
- Boot 裁剪认证、转发、分片及乱序。PHY 返回前必须消费或复制字节；驱动可以使用 containers 的 DMA 缓冲接口，但这不改变协议 PHY 的同步所有权契约。
- ACK 状态使用紧凑位集合和循环偏移；128 个状态占用 16 字节位存储，完整资源仍以工作区计算与目标 ELF 为准。

## Validation / 验证

```sh
cmake --preset ci \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build build/ci --target xgl_release_validation --parallel
```

参见[验证矩阵](docs/zh/reference/validation-matrix.md)、[迁移指南](docs/zh/guide/modular-migration.md)、[Boot 升级示例](examples/boot_update/README.md)及 [Cortex-M0 占用探针](tools/boot_footprint/README.md)。主机测试及 ELF 链接不替代板级 Flash、断电、栈和密码 provider 验收。
