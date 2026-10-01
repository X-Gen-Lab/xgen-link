# Datalink 有界接收与资源优化验证

日期：2026-10-01。起点 `d7bbb75`，设计见 [ADR 0008](../adr/0008-datalink-runtime.md)，使用说明见[中文](../../docs/zh/reference/datalink-optimization.md)与[英文](../../docs/en/reference/datalink-optimization.md)。本记录区分行为、构建、静态资源与板级验收。

## 实施与检查点

| 检查点 | 实际证据 |
| --- | --- |
| `3ffaa6d` parser RED | 新增 8 项真实生产 GoogleTest；4 项失败，复现重叠 magic、时钟回绕、固定头立即拒绝与 required-view 契约 |
| `b819ad3` parser GREEN | 同组 8/8；保持绝对期限、最终完整 wire 校验和兼容字节入口 |
| `f9abec3` span RED | 8 项新契约编译时因缺少消费入口失败，随后由真实生产实现转绿 |
| `404bff7` 接收结构 | 合并 metadata、同步 span、紧凑 parser 与四个单向端点；GNU Full 646/646 |
| `9d02fc6` chunk RED | 7 项配置/安装/报告契约失败，暴露默认值、无效覆盖、错误容量接受及缺失资源身份 |
| `27256a9` chunk GREEN | 7/7；受控 AUTO 和显式值、安装头冲突拒绝、目标布局格式 4、独立 chunk 预算身份 |
| `b75eac7` smoke 调度 | GNU/MSVC Boot 旧单次读 ACK 假设真实退出 8；按有界 poll 排空各阶段后，GNU 两项及 MSVC 完整矩阵通过 |

纯端点、文件与状态布局调整使用既有回归，不人为制造行为失败。原有 510 个测试名称完整保留。RED/GREEN 产物在 `out/parser-opt`、`out/datalink-rx`、`out/datalink-opt-root` 和 `out/datalink-opt-msvc`，均为本地诊断产物，不进入产品依赖。

smoke 修复不仅补 ACK 读取，也在应用 BUSY 阶段完整接收首次 DATA，确保真实触发拒收；随后验证重传交付、ACK 后窗口复用、反向可靠发送和时钟回绕。循环有明确上界，保留原断言，没有改动协议以迁就测试。

## 主机、契约与质量

| 检查 | 本地结果 |
| --- | --- |
| Windows GNU Full / Embedded | 各 646/646 CTest，639 GoogleTest 加 7 项 smoke/示例 |
| Windows GNU Boot / Embedded Lite | 95/95、130/130 CTest；Boot 93 GoogleTest 加 2 项 smoke |
| MSVC Full / Boot | 646/646、98/98；Boot 另开启 3 个示例，包含 SDK 安装消费 |
| 独立头文件 | 28 个头分别 C11/C++17，共 56 个翻译单元；删除的 metadata 私有头不再存在 |
| 生产依赖 / 开发准备 / 发现 / runner | 11、11、3、6 项通过 |
| profile 与多配置消费 | 4 项通过，包括 Debug/Release 与安装能力检查 |
| chunk 与资源契约 | 新增 7 项、原预算 5 项、LTO 4 项通过；Linux LTO 4 项也通过 |
| 额外读取块消费 | Boot 16、64、128 各自的静态 workspace 与已安装 SDK smoke，共 6/6 |
| Linux GNU Full | 646/646，包含安装消费者 |
| Linux ASan / UBSan | 645/645，含泄漏检查；独立安装消费者由普通 Full 验证 |
| 生产覆盖率 | 行 3822/4145（92.2%）、函数 285/286（99.7%）、分支 2425/2966（81.8%） |
| 静态分析 | 固定 Cppcheck 2.21.0 与 Clang-Tidy 19.1.0 通过，43 个生产 C 文件及相关头 |
| 格式与文档 | clang-format 19.1.5、全仓 pre-commit、严格 Doxygen/MkDocs、双语导航与文档 QA 通过 |

Windows GNU 13.2、MSVC 19.40.33811、Linux GNU 11.4 分别执行。GoogleTest 固定 1.16.0，C++17 严格模式；没有删除历史测试、跳过失败测试或从覆盖率中排除难测生产文件。

主要证据：`out/datalink-opt-root/final/native-*`、`out/datalink-opt-msvc/audit.json`、`out/datalink-opt-contracts/verification-summary.json`、`out/datalink-opt-root/linux/evidence`、`out/datalink-opt-rx-review/cppcheck`。Windows 四 profile 的最终矩阵输入哈希前后一致；MSVC 的生产源、头及 CMake 哈希前后一致，HEAD 与文档脏状态按实际保存。

Linux 最初预检查使用配置修改前生成的头，静态分析报缺少 `XGL_BUILD_RX_CHUNK_SIZE`。保留 `tidy-stale-header.log`；重新配置、编译两个 Linux 构建，清除旧覆盖率样本，重跑 Full、覆盖率、ASan/UBSan 和 tidy 后均通过。没有忽略编译诊断或添加分析豁免。最终 Linux 记录对应 `b75eac7` 的协议与消费代码。

## Cortex-M0 最终 ELF

Arm GNU 15.2.rel1 / GCC 15.2.1、`-Os`、Boot、单 peer/route/link、窗口 1、RX cache 等于 MTU、chunk32。外部 PHY 消费者和布局对象不参与 LTO，避免把已知消费代码内联后消除协议工作。每组额外预留 1,024 B 栈。

| MTU/RX | LTO | Flash | workspace | 静态 RAM | 含预留栈 RAM | 最大单函数栈 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 128 | OFF | 15,988 B | 1,112 B | 1,176 B | 2,200 B | 192 B |
| 64 | OFF | 15,988 B | 920 B | 984 B | 2,008 B | 192 B |
| 128 | ON | 12,600 B | 1,112 B | 1,176 B | 2,200 B | 224 B |
| 64 | ON | 12,600 B | 920 B | 984 B | 2,008 B | 224 B |

Windows 与固定官方 Linux 工具链四组合各自通过，所有数一致，无堆服务和未解析符号。两系统构建前的 175 个输入文件逐项 SHA 相同，见 `source-comparison.json`；路径排序造成汇总摘要不同，保留实际报告而不伪称摘要相同。Windows 汇总位于 `final/arm-summary.json`，Linux 位于 `linux-arm/summary.json`。

旧 chunk128 的非 LTO Flash 为 15,932 B，本轮增加 56 B；LTO 保持 12,600 B。工作区各减少 24 B：实例端点减少 16 B、每 link 减少 8 B。无 LTO 的 linked datalink 为 1,366 B，相比旧 1,290 B 增加 76 B；parser 472 B、receive 420 B、send 330 B、入口 144 B，完整 ELF 同时包含其他层及对齐变化。

旧非 LTO/LTO 最大单函数栈为 272/312 B，本轮为 192/224 B。热路径 feed view 为 40 B，poll 为 80 B，嵌套 span 为 136 B；这些不能只取较小一项充当完整栈。旧字节兼容入口自身仍使用 104 B，未进入该 Boot ELF。

四份预算收紧至实测 workspace、静态 RAM、总 RAM 与单函数栈；Flash 上限保持 16,384/12,800 B，未放宽。八份现有 ELF 报告重新审计全部通过，见 `tight-budget-results.json`。历史基线与 chunk128 来源保留，不用新值覆盖旧测量。

## 验收边界

span 是同步输入复用接口，内部仍逐字节处理；未新增 public DMA API、跨调用借用、线程或隐式堆。协议资源服务继续来自独立组件，各资源池所有权不合并。

资源探针不含真实 BSP、UART/DMA、Flash 写入、看门狗、升级业务及生产认证 provider。预留栈与 `.su` 不是完整调用链或 ISR 高水位。8 KiB Flash 目标仍未达到，MTU64 workspace 小于 1 KiB；未执行板级验收。本地提交、远端 CI 与发布分别记录，未推送、合并 main 或创建 tag。
