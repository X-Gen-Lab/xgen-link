# Wire 布局、借用视图与体积优化验证

日期：2026-10-01。起点为 `0e08bdf`，协议实现检查点为 `4a10065`，LTO 工具与示例修复检查点为 `37f5d59`。设计见 [ADR 0007](../adr/0007-wire-layout-views.md)，使用说明见[中文](../../docs/zh/reference/wire-optimization.md)与[英文](../../docs/en/reference/wire-optimization.md)。本记录区分源码、主机执行、交叉链接和板级验收。

## 实施与检查点

| 范围 | 证据 |
| --- | --- |
| 长度回绕、非法 TLV/tag 开销及 payload 上限 | `7f1e72a` 加入并运行 3 项失败测试；`cca3482` 同组转绿，frame 29 项通过 |
| SACK 借用视图及 64/65 字节反馈边界 | `93d44c9` 记录缺少新 API 的编译 RED；`4a10065` 保持旧复制入口并去掉 transport 位图复制 |
| RX 重构契约 | 4 项契约先在旧实现通过，再精简 CRC 副本和临时 header；相关 152 项通过，不人为制造 RED |
| 发送布局与源文件收敛 | `4a10065` 复用当前同步调用的 checked layout；整合 Full 630/630 CTest |
| 显式 LTO 及模式隔离 | `4925b19` 真实配置/预算 RED；`37f5d59` 新 4 项及原预算 5 项通过 |
| Boot 示例关闭统计 | MSVC Boot 真实退出 5；`37f5d59` 将统计断言限定于启用统计的配置，保持丢 ACK 后完整更新与仅写一次检查 |

RED、GREEN 与中间失败日志分别保留在 `out/wire-opt-root`、`out/wire-opt-sack`、`out/wire-opt-rx`、`out/wire-opt-msvc`。这些为本地诊断产物，不作为发布源码依赖。原有 510 项协议测试名称完整保留。

## 主机与质量

| 检查 | 本地结果 |
| --- | --- |
| Windows GNU Full / Embedded | 各 630/630 CTest，623 个 GoogleTest 加 7 项 smoke/示例 |
| Windows GNU Boot / Embedded Lite | 79/79、114/114 CTest，按能力选择测试 |
| MSVC Full / Boot | 630/630、82/82；Boot 另开启示例，77 个 GoogleTest 加 5 项 smoke/示例 |
| 独立头文件 | 29 个头分别首包含，C11/C++17 共 58 个翻译单元 |
| 契约 | GNU 生产依赖 11、开发准备 11、发现策略 3、runner 6、profile/多配置消费 4 项通过 |
| Linux GNU Full | 630/630，包含安装消费 |
| Linux ASan / UBSan | 629/629，包含泄漏检查；独立安装消费者由普通 Full 验证 |
| 生产覆盖率 | 行 3822/4147（92.2%）、函数 283/284（99.6%）、分支 2420/2962（81.7%） |
| 分析范围 | 44 个生产 C 文件及有执行代码的私有头，未移除覆盖率分母或历史测试 |
| 固定质量入口 | Cppcheck、Clang-Tidy、clang-format 19.1.5、pre-commit、严格 Doxygen/MkDocs、双语文档 QA 通过 |

Windows GNU 13.2、MSVC 19.40.33811、Linux GNU 11.4 的结果各自保留。GoogleTest 固定 1.16.0，主机测试采用严格 C++17。Windows 四配置最终矩阵的完整输入哈希前后一致；MSVC 的 `src/include` 哈希一致，全输入记录如实保留并行文档/工具及示例修改。

证据目录：`out/wire-opt-root/final/native-*`、`out/wire-opt-msvc`、`out/wire-opt-root/linux/evidence`。MSVC 不支持 GNU coverage 插桩，依赖契约的完整 11 项由 GNU 执行，未把 MSVC 对不支持组合的拒绝写成通过。

本地 sanitizer 脚本初次误开启独立 SDK 消费者：插桩静态库的独立链接缺少 sanitizer 运行时，630 项中只有该安装链接失败，协议测试均通过。保留失败日志 `out/wire-opt-root/linux/asan-test.log`；将该诊断配置恢复为 `XGL_BUILD_SDK_CONSUMER_SMOKE=OFF` 后重新构建并实际运行 629 项。没有修改生产实现或忽略协议失败，普通 Full/MSVC/GNU 矩阵继续执行安装消费者。修复统计裁剪示例后，Linux Full 和 sanitizer 均重编、重跑。

## Cortex-M0 最终 ELF

固定 Arm GNU 15.2.rel1 / GCC 15.2.1、`-Os`、Boot、单 peer、单路由、单 link、窗口 1。默认关闭 LTO；启用时仅库与最终链接参与 LTO，外部 PHY 消费者和布局对象仍为普通对象。

| 配置 | Flash | workspace | 静态 RAM | 预留栈 | RAM 合计 | 最大已链接单函数栈 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| MTU/RX 128，无 LTO | 15,932 B | 1,136 B | 1,200 B | 1,024 B | 2,224 B | 272 B |
| MTU/RX 64，无 LTO | 15,932 B | 944 B | 1,008 B | 1,024 B | 2,032 B | 272 B |
| MTU/RX 128，LTO | 12,600 B | 1,136 B | 1,200 B | 1,024 B | 2,224 B | 312 B |
| MTU/RX 64，LTO | 12,600 B | 944 B | 1,008 B | 1,024 B | 2,032 B | 312 B |

四份 Windows 交叉报告、ELF、map、布局、`.su`、反汇编和预算结果在 `out/wire-opt-root/final/arm-*`；全部无堆或未解析符号。报告保留测量时的真实 revision、dirty 状态和源码哈希 `f09cb743bf567453875bb87c5f6a24c7cf0e1f5a8e4b58aa0d9968ffcd012a8f`，不将尚未提交的测量树冒称干净发布提交。

固定官方 Linux ARM 工具链也执行新 LTO 契约 4/4、四组完整构建与四个预算，全部通过；数字和优化参数与 Windows 一致。证据在 `out/wire-opt-root/linux-arm`，运行前后输入一致，96 个生产与探针文件逐个 SHA 与 Windows 相同。原报告生成器按平台路径排序，Linux 全局摘要为 `9df0d5236d8ab7df272c910211a3dd0c74c9b52677316d9af62329fcf7d1e759`，因此保留两个实际摘要，不将跨系统排序差异写成相同摘要，也不忽略逐文件比较。

同配置普通 ELF 从 16,060 B 降低 128 B；其中实际链接的 wire 从 2,552 B 降到 2,400 B，其他层及对齐变化已计入整镜像。原 LTO 实验为 12,472 B，本轮完整边界修复后为 12,600 B，增加 128 B；最终源码开启 LTO 相比自身普通构建减少 3,332 B，最大单函数栈增加 40 B。不能用旧实验值替换本轮结果，不能把 LTO 后残存符号相加当作 wire 总大小。

相同非 LTO 编译条件下，头 CRC 单函数栈 32→16 B，完整帧解码 136→96 B，ACK/SACK 处理 168→128 B。没有新增每 link/peer 常驻状态，workspace 不变。parser 跳过第二次固定头 CRC 的原型增加 56 B Flash，未进入生产；零拷贝辅助函数原本未进入 Boot ELF，文件合并不计作体积收益。

普通预算保留既有上限，LTO 单独限制 Flash 12,800 B、最大单函数栈 320 B。两类报告不能混用预算。CI 已配置 MTU128/64 × LTO OFF/ON 四组合，远端是否执行须看本次提交实际 Actions，不沿用历史 CI 结论。

## 验收边界

wire v3、公开 ABI、认证 nonce 规则、借用生命周期和反馈事务性保持。生产不新增堆、基础组件副本或运行时插件。SACK 仍限制 transport 接受最多 64 字节位图。

完整 ELF 是通用探针，包含最小启动和消费者，但不含真实 BSP、UART/DMA、Flash 写入驱动、看门狗和升级业务。1024 B 是栈预留；`.su` 是单函数静态栈，不等于完整调用链或 ISR 嵌套峰值。两种编译方式均未达到 8 KiB Flash 目标，真实板卡与生产密码 provider 未验收。未推送、合并 main 或创建发布 tag。
