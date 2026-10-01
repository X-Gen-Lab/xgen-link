# 生产依赖与开发装配分离验收

日期：2026-10-01。分支：`feat/independent-memory-containers`。

本轮只调整依赖归属、开发入口和相关检查。沿用已有五包协议迁移的工作区，不改变协议运行时行为，不把历史结果标成本轮重跑。

## 最终边界

- 生产根 CMake 只接受父工程提供的兼容 targets，或通过 `find_package` 消费已安装包。五包版本、ABI、target 类型和同包版本一致性检查保留。
- 五个 `external/xgen-*` 子模块工作树和注册已移除，`.gitmodules` 与索引 gitlinks 已撤销。它们在本轮开始时仍是暂存新增，未进入 HEAD；Git 历史对象和独立兄弟仓库保留。
- 旧 `XGL_{STATUS,BYTES,CRC,MEMORY,CONTAINERS}_SOURCE_DIR` 明确报错，不再加载源目录或旧 `external` 目录。旧构建缓存需要清理这些选项。
- 产品负责同一镜像的能力并集及唯一依赖版本；link 不再写入基础库的能力配置。作为子目录时，示例、smoke、SDK 消费、资源报告、静态分析和发布辅助默认关闭。
- `dev/` 通过五个 `XGL_DEV_*_SOURCE_DIR` 显式装配源码，供独立开发和资源诊断使用，不下载依赖、不猜测兄弟路径。不同 profile 使用独立构建目录。
- `dev/dependencies.json` 固定本仓 CI 测试输入，不决定产品的生产依赖。CI 显式 checkout 并验证提交；配置和编译不获取依赖。

## TDD 与构建契约

生产契约位于 [test/cmake](../../test/cmake/README.md)，使用真实 CMake 配置、构建、安装及 C 消费者执行。

| 阶段 | 实际结果 | 证据 |
| --- | --- | --- |
| 任务前基线 | 8/8 CTest | `build/dependency-ownership/baseline.xml` |
| 生产契约 RED | 完整 10 项中 5 项失败、5 项通过；子测试合计 9 个失败 | `build/dependency-ownership/contracts-red-full/run-l2k9tzzj/result.json`；检查点 `307d326` |
| 开发入口 RED | 2 项失败，缺少入口及明确缺路径诊断 | 检查点 `549e0b9` |
| 生产契约 GREEN | 10/10 通过 | `build/dependency-ownership/contracts-green/run-n83hm6jq/result.json` |
| 开发装配与 CI 输入 | 11/11 Python unittest 通过 | `build/dev-validation/`；`dev/tests/test_development.py` |
| CI 输入脚本覆盖 | 行 56/57（98.25%）；分支 27/28（96.43%），无排除 | `build/dev-validation/coverage.json` |

生产契约验证：父工程能力并集复用、安装包来源及 IMPORTED 身份、子目录开发开关默认关闭、五个旧参数拒绝、旧 external 哨兵不执行、缺包与不完整 targets 拒绝、版本/ABI/类型错误拒绝，以及 Full/Boot 分别构建运行。额外能力 arena 和 ring-buffer 由上层选择并实际运行，不能仅凭配置成功认为完成去重。

开发入口验证包含缺路径/相对路径诊断、固定清单 schema、仓库名、提交 SHA、真实 Git checkout 身份与干净状态、CLI 输出和失败退出。覆盖数字仅属于新增 `prepare_dependencies.py`，不代表协议整体覆盖率。

## 新入口实际构建

| 场景 | 工具链及结果 | 本地证据 |
| --- | --- | --- |
| Full 开发装配 | GCC 13.2；8/8 CTest，内含 510/510 GoogleTest；包括 SDK 安装消费 | `build/dev-integration-full/Testing/Temporary/LastTest.log` |
| Embedded 开发装配 | GCC 13.2；4/4 CTest，内含 510/510 GoogleTest；生产 fallback malloc 关闭 | `build/dependency-ownership/embedded.xml`；`build/ownership-embedded/Testing/Temporary/LastTest.log` |
| Boot 开发装配 | GCC 13.2；2/2 CTest，静态工作区与 SDK 安装消费 | `build/dependency-ownership/boot.xml` |
| 开发预设 | 五个显式环境路径及 GTest_ROOT 完成 gcc-test 配置；10 个预设可列出 | `build/dev-preset-validation/` |
| 文档 | 新 dev 顶层下 Doxygen 与 MkDocs strict 构建通过 | `build/dependency-ownership/docs-build.log`；`build/ownership-docs/link/docs/site/` |
| 源码排版 | 固定 clang-format 19.1.5 检查与 pre-commit 通过 | `out/reports/quality-format.json` |
| 静态分析 | 新 dev 编译数据库的 126 个协议编译条目通过 cppcheck | `build/dependency-ownership/static-analysis.log` |
| 文档一致性 | 文档 QA、25 个变更文档的 UTF-8/LF 与 66 个本地链接检查通过 | `tools/docs_qa.ps1`；本轮文档检查 |

SDK 检查从 dev 顶层安装所选基础库与 link，再单独配置、构建并运行消费者。生产安装包入口仍支持显式上游 prefix/package 提示。文档源路径使用 link 的 `PROJECT_SOURCE_DIR`，编译数据库使用构建树的 `CMAKE_BINARY_DIR`。

## Cortex-M0 资源复核

使用 GCC Arm 15.2.1、`-Os`、无 LTO，在新的 `build/ownership-arm` 中显式提供五个源码路径，链接完整资源探针。

| 指标 | 本轮 | 与前轮最终值比较 |
| --- | ---: | ---: |
| Flash | 16,764 字节 | 不变 |
| 静态 RAM | 1,488 字节 | 不变 |
| 其中 workspace | 1,424 字节 | 不变 |
| 额外预留栈 | 1,024 字节 | 不变 |
| RAM 含预留栈 | 2,512 字节 | 不变 |
| 最大已链接单函数栈 | 392 字节 | 不变 |

报告：`build/ownership-arm/boot-footprint.json`。没有堆服务或未解析符号。workspace 已计入静态 RAM；预留栈不是板上高水位，单函数栈不是完整调用链上界。满足测量镜像的 64 KiB Flash / 8 KiB RAM 限额，仍未达到此前 8 KiB Flash / 1 KiB workspace 的更小目标。

报告记录实际源码路径、HEAD 和 dirty 状态：link 以及 status/bytes/CRC 存在既有工作区改动，memory/containers 干净。这是本地工作区验证，不是干净产品发布，也不代表上板验收。

## 交付状态与后续门槛

本轮检查点记录测试及构建改动；已有协议重构仍保留在工作区，不将全部既有变更混入一个提交。不能声称单独检出检查点就包含全部受测代码。未推送任何仓库，也未创建远端。

CI 工作流与 YAML 已完成本地检查。远端执行前仍需发布五包固定提交，并配置五个 `XGEN_*_REPOSITORY` 变量；质量工具还需要已有的 `XGEN_QUALITY_REPOSITORY`。远端 CI、ASan 执行、MSVC 新入口复核和硬件测试未在本轮运行。ASan 预设明确关闭 SDK 消费测试，构建并测试其余全部已选目标；SDK 由上表的普通构建独立覆盖。

复现入口见 [开发装配说明](../../dev/README.md)、[依赖契约测试](../../test/cmake/README.md)和 [Boot 探针](../../tools/boot_footprint/README.md)。本地 `build/` 与 `out/` 证据不进入版本控制，CI 已设置契约结果归档。
