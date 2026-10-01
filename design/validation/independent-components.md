# 独立基础组件迁移验证

本记录区分已执行的主机验证、ARM 交叉链接及尚未执行的硬件验证。
link 原有工作区改动保留；不把这些改动全部打包进本轮检查点提交。

## 来源与基线

- 实施分支：`feat/independent-memory-containers`。
- 原始工作区、暂存区及 HEAD 记录位于本地
  `build/independent-migration/working-baseline.zip`、`baseline-*.txt`
  和 `staged.patch` / `unstaged.patch`，属于未安装的开发产物。
- core 来源：`dc5eb1167ba21de384e7a760a8586b87502b73b2`。
- GCC 13.2：旧 core 11/11 CTest（含安装消费）通过。
- GCC 13.2：旧 link 8/8 CTest（包含实际 GoogleTest 集合、四个示例、
  静态工作区、无堆及安装消费）通过。
- GCC 15.2.1 Cortex-M0：原探针 Flash 16,212 B、静态 RAM 1,480 B、
  workspace 1,416 B、预留栈 1,024 B、RAM 合计 2,504 B。

## ACK 位存储的 TDD 证据

RED 检查点：`b169a10574b1d2a3929ef0d331a35d6ccd460c5c`。
先增加 `CompactWindowTest` 并在原实现执行四个用例，其中两个失败：

1. 128 个 ACK 状态申请 128 B，没有满足 16 B 位存储契约。
2. 初始化分配失败后，调用者原有 allocator 字段被修改。

非字节对齐窗口反复复用，以及旋转后重置的两个回归用例通过。
失败证据在本地 `build/independent-migration/window-red.xml` 和日志。

GREEN：生产实现消费 `xgct::bitset`，保留紧凑借用视图与一字节循环偏移；
128 个标志申请 16 B，成功前不发布新窗口状态。相同四个用例全部通过。
证据在 `window-green.xml` 和日志；完整新依赖构建的 8/8 CTest 也通过。

执行入口：

```text
cmake --build build/independent-full --parallel 6
ctest --test-dir build/independent-full --output-on-failure --no-tests=error
build/independent-full/test/xgl_tests.exe --gtest_filter=CompactWindowTest.*
```

本检查点提交记录可重放测试和实际结果，生产迁移保留在工作区供整体评审，
不声称该提交单独包含此前尚未提交的完整协议重构。

## 接口与资源边界

独立组件使用 `xgm::`、`xgct::`、`xgb::`、`xgcrc::`、`xgs::` targets。
公开 allocator 类型随新组件迁移；wire v3 不因此更改版本。
配置不下载依赖，主机测试显式提供 GoogleTest/GoogleMock 1.16.0。

containers 的 DMA 缓冲接口不改变 link PHY 的同步消费契约。
DMA 时序、缓存维护、可访问内存和中断交接需要具体板级验证。

目标探针使用通用 Cortex-M0 布局，不是完整可烧录 Bootloader；
静态 RAM、链接器预留栈和实际调用链峰值分别记录。
不从主机通过推断硬件通过。

## 最终本地验收（2026-10-01）

五个 Git 子模块已实际接入，旧 `external/xgen-core` gitlink 已移除。
子模块使用相对仓库 URL，本工作区以 Git 本地配置指向兄弟仓库；
尚未创建或推送新的远端仓库，不能据此声明远端递归克隆或 CI 已通过。
本地准备命令见 [实施状态](../../REFACTORING_STATUS.md)。

| 组件 | 固定提交 |
| --- | --- |
| status | `28bc7be369b81e9b9b9d7379743dd0da6fa4647b` |
| bytes | `960038992b30ac9c54bef190e9acabe86239f8fc` |
| CRC | `ec4c73d21771c57f3abfb33fc686d2d9b9585288` |
| memory | `1360e489ca97a594a47968572d6b6c833aac532f` |
| containers | `c2997e0b500ef64af282961b0d471ba8b79d9954` |

memory 的实现与完整质量验证基线是其父提交 `dace87da`；后续仅澄清
pool 描述符与块存储不得重叠的既有前提，格式和严格 Doxygen 再次通过。
core 的归档提交为 `b042d51abf45b411a8bf1fdbd98337270f8d024c`，
保留历史与来源说明，当前不再包含生产实现、导出目标或 tiered 包装。

| 验证 | 实际结果 |
| --- | --- |
| memory：GCC 13.2 / MSVC 19.40 | 各 23/23 CTest；22 组 GoogleTest，加涵盖 15 场景的集成检查 |
| containers：GCC 13.2 / MSVC 19.40 | 各 33/33 CTest；16 组 GoogleTest，加 17 个消费检查 |
| memory 行 / 函数 / 分支覆盖 | 99.3% / 100% / 93.2% |
| containers 行 / 函数 / 分支覆盖 | 99.4% / 100% / 98.3% |
| link Full：GCC / MSVC | 各 8/8 CTest；实际各运行 510 个 GoogleTest，全部通过 |
| link Embedded：GCC，关闭堆回退 | 4/4 CTest；实际运行 510 个 GoogleTest，全部通过 |
| link Boot：默认五子模块、关闭堆回退 | 2/2 CTest，静态生命周期与安装消费通过 |
| link 三个 benchmark | Full、Embedded 均构建及运行通过；仅代表主机模拟数据 |
| link 格式 / 静态分析 | 148 个自有 C/C++ 文件格式检查、实际编译数据库 Cppcheck 通过 |

新组件各自通过 pre-commit、Cppcheck、clang-tidy、严格 Doxygen、
公开头的 C/C++ 独立包含，以及源码和安装消费者验证。
完整证据分别保存在组件的 `docs/validation.md` 与本地报告目录。
协议测试 XML 和日志位于 `build/independent-migration/`；这些构建产物
不安装进 SDK，也不作为源代码提交。

额外的独立 bool 模型对 128 种窗口宽度各执行 5,000 步，合计
640,000 步验证 ACK、槽位复用与存储哨兵；关键非零 head、物理末尾
回绕和完整 128 位推进已固化为第 5 个 `CompactWindowTest`。

安装接口还验证了完整预提供 target 集合的兼容补丁版本、错误版本、
ABI、目标类型及真实缺包场景。完整集合使用相同的兼容范围；不完整
集合仍交给提供者校验，禁止把不同版本的实现拼在一个组件中。
同一组件的所有目标还必须使用同一精确版本；完整集合混用 0.1.0 与
0.1.1 的源码和安装负例均先复现放行、后修复拒绝。统一使用 0.1.1
的组件仍可与其他 0.1.0 组件组合，不强制不同仓库同步版本。
修复了安装 SDK 验证未传播上游依赖路径的问题，并保留真实 RED/GREEN 日志。
三个主机 benchmark 也显式链接自己的 libc allocator；关闭生产堆回退
时缺符号的链接失败已复现并修复，未因此修改生产配置。对应证据为
`benchmark-noheap-red.log`、`benchmark-noheap-green.log` 与主机 CSV。

## 最终目标资源

`build/independent-arm-final/` 使用默认子模块、GCC 15.2.1、Cortex-M0、
`MinSizeRel`、无 LTO 的 Boot 配置。五个提供者均为上述干净固定提交；
JSON 同时记录 link 的未提交状态与实际源码散列。

| 项目 | 旧基线 | 新实现 | 差值 |
| --- | ---: | ---: | ---: |
| Flash | 16,212 B | 16,764 B | +552 B |
| 静态 RAM | 1,480 B | 1,488 B | +8 B |
| workspace | 1,416 B | 1,424 B | +8 B |
| 预留栈 | 1,024 B | 1,024 B | 0 B |
| RAM 合计 | 2,504 B | 2,512 B | +8 B |
| 最大已链接单函数栈 | 392 B | 392 B | 0 B |

最终 ELF 无堆服务和未解析符号。满足该测量镜像的 64 KiB Flash /
8 KiB RAM 限额，尚未达到另列的 8 KiB Flash / 1 KiB workspace 设计目标。
窗口位存储压缩不等同于整个镜像体积必然下降；不虚构上述增量的对象级归因。

## 尚未覆盖的验证

本轮未执行 sanitizer、远端 CI、真实 DMA/缓存一致性/ISR 并发、
板级启动、Flash 写入、断电恢复或实际调用链栈高水位验证。
link 现有测试保持 C++20 与单个 CTest 聚合入口；其既有 property
测试使用 concepts。新组件使用 C++17 与 GoogleTest 测试发现。
未测量 link 全量覆盖率，不将独立组件的覆盖率结论外推至协议库。
这些差异属于后续规范迁移与硬件验收工作，不标记为已达标。
