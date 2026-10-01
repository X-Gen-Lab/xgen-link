# GitHub CI

## 固定输入与触发范围

`.github/workflows/ci.yml` 在 `main`、`develop`、`feat/**` 的 push、目标为 `main` 或 `develop` 的 pull request，以及手动触发时运行。最终 `status` 任务要求所有选中的任务成功；失败、取消和异常跳过不会被归为通过。Pages 工作流仅接受 `main` 的 push 或手动触发，功能分支的 CI 不会触发自动部署。

CI 从 `dev/dependencies.json` 读取 status、bytes、CRC、memory、containers 的固定提交。`setup-components` 显式检出并验证提交，然后通过五个 `XGL_DEV_*_SOURCE_DIR` 配置 `dev`；获取源码与 CMake 配置分别执行。生产模块只消费上层 targets 或已安装包，产品仍自行固定每个镜像的唯一组件组合。

共享质量工具的唯一来源声明是 `tools/quality.json`。`setup-quality` 先安装对应提交的包；Linux 分析任务另显式构建固定提交的 Cppcheck 和 Doxygen。GoogleTest/GoogleMock 1.16.0 由 `setup-gtest` 准备，使用与主机工程一致的原生编译器。配置过程不下载、不选择最新版本。

| 可选仓库变量 | 未设置或为空时的地址 |
| --- | --- |
| `XGEN_QUALITY_REPOSITORY` | `X-Gen-Lab/xgen-quality` |
| `XGEN_STATUS_REPOSITORY` | `X-Gen-Lab/xgen-status` |
| `XGEN_BYTES_REPOSITORY` | `X-Gen-Lab/xgen-bytes` |
| `XGEN_CRC_REPOSITORY` | `X-Gen-Lab/xgen-crc` |
| `XGEN_MEMORY_REPOSITORY` | `X-Gen-Lab/xgen-memory` |
| `XGEN_CONTAINERS_REPOSITORY` | `X-Gen-Lab/xgen-containers` |

变量只覆盖 `owner/repository`，不会覆盖固定提交。私有派生仓库还需提供具备读取权限的检出凭据。

## 检查矩阵

| 任务 | 环境与实际检查 |
| --- | --- |
| `host` | Ubuntu 24.04、Windows 2022/MSVC、macOS 15；文本、排版、pre-commit、C11 协议、严格 C++17 GoogleTest、示例、SDK 消费，以及测试发现和种子回放契约 |
| `analysis` | Ubuntu 24.04；原生 GNU 插桩构建、共享测试入口、Cppcheck、Clang-Tidy、严格公共 API Doxygen、行/函数/分支各 80% 门槛、发布辅助与文档站点 |
| `sanitizers` | Ubuntu 24.04；ASan/UBSan，先构建全部选中目标，再运行共享测试入口 |
| `bounded-profiles` | 分别构建 Boot 和 Embedded 的 C11 无 libc fallback 配置，执行静态生命周期及安装消费 |
| `dependency-contracts` | dev 准备工具单测、真实父工程 target 复用、安装消费与缺失/不兼容依赖拒绝 |
| `status` | 汇总全部必要任务，任何非成功结果都失败 |

Host 使用 `cmake -S dev --preset debug`，输出到 `build/dev-debug`。分析、sanitizer、受限配置分别使用 `dev-ci`、`dev-asan`、`dev-boot` 和 `dev-embedded`。这些是不同构建目录，不共享 ABI、profile 或覆盖率计数。源码开发的 CTest 根目录是 `build/dev-<preset>`。

GoogleTest 逐用例登记并标记 `unit`、`integration`、`property`。发现阶段逐名保留迁移前的 510 个测试；共享 runner 检查选中清单与实际 JUnit 一致，并拒绝空集合、禁用、跳过和失败。运行命令及回放方法见[测试策略](../contributing/testing.md)。

ASan 预设关闭安装包 smoke，因为全局 sanitizer 参数不会自动成为外部消费者的链接要求；安装边界由 Full 和独立消费契约覆盖。Boot/Embedded CI 任务不执行 GoogleTest，主机协议回归由 Host 任务承担。

## 产物与执行结论

任务通过 `always()` 保存适用的 `out/reports/`、CTest 日志、编译数据库、Doxygen 诊断或资源报告。报告记录工具与源码身份、配置、选中测试和实际退出结果；后续检查失败时仍保留此前证据。必要产物缺失会失败。

本地检查、远端运行和产品验收分别记录在根目录 `REFACTORING_STATUS.md`；工作流存在不代表某个提交已经通过。板级运行、真实密码 provider、断电恢复与最终 MCU 分区验收由产品补齐，见[发布验证](release-validation.md)。
