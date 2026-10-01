# Network 契约与资源优化验证

日期：2026-10-01。实施前基线为 `15202dd`；生产冻结点为 `734b7a0`，最终测试冻结点为 `e8794c3`。后者仅补充 scoped 适配器的借用与身份回归，不改变生产输入。本记录区分最终 ELF、主机测试、对象级实验和未执行的硬件验收。架构决定见 [ADR 0010](../adr/0010-network-contracts.md)。

## 范围与 TDD

- `a907c68`：接收、地址和转发 RED，6 例中 5 例按预期失败；另记录属性种子 15 的真实失败。
- `e0d7e67`：相同 6 例全部 GREEN，修正属性测试发送者生成前置条件，保留生产的 source/target 不同规则。
- `88e9a04`：静态路由与零拷贝计数 RED，11 例中 5 例失败，其他 6 例为既有契约护栏。
- `d2d1ded`：路由/配置/工作区/链路 76 例、发送 32 例、接收/转发/属性组合 74 例通过。历史无 PHY 发送函数的断言改为透传 INVALID_PARAM，保留测试名称和拒绝语义。
- `734b7a0`：去掉扩展整数组清零，容量不变；32 例发送回归、6 例真实三节点集成和 4 例分配/计数验证通过。
- `e8794c3`：保留的私有 scoped 入口以非零身份、原始 PHY 缓冲指针及短容量拒绝验证；公开 API 改走共同 helper 后，该适配入口继续有独立覆盖。

证据位于 `out/network-opt/root/{red,green,routes-tx-red,refactor2,coverage-cases}.xml`、`out/network-opt/routes/focused.xml`、`out/network-opt/tx/{green,refactor}.xml`、`out/network-opt/multihop/ctest.xml`。多跳首轮测试遗漏可靠 HELLO 的失败单独保留为 fixture 修正记录，不冒充生产 RED。

## 主机与工具验证

| 组合 | 结果 |
| --- | --- |
| Windows GNU Full / Embedded | 各 697/697 CTest，含 690 个 GoogleTest；runner 契约各 6/6 |
| Windows GNU Boot / Embedded Lite | 128/128、169/169 CTest |
| Windows MSVC Full / Boot | 697/697、131/131；Boot 比 GNU 多启用三个示例；Full runner 6/6 |
| Linux GNU Full / ASan+UBSan | 697/697、696/696，无失败、跳过或禁用；sanitizer 关闭独立 SDK 消费测试 |
| Linux 生产覆盖率 | 行 3869/4174（92.7%）、函数 290/291（99.7%）、分支 2484/3004（82.7%） |
| Clang-Tidy | 19.1.0，固定质量入口通过，43 个生产 C 翻译单元 |
| Cppcheck | 固定质量入口通过，43 个生产 C 翻译单元 |
| 格式、文本、文档 | 固定格式、文本、严格 Doxygen/MkDocs、全仓 pre-commit 与双语 QA 全部通过 |

Windows GNU 命令与输入摘要位于 `out/network-opt/root/final`；构建前后受测输入一致。四个 profile 保留各自能力选择、历史名称检查、独立 C11/C++17 头编译和安装消费。属性种子 15 的失败及修复回放日志位于 `out/network-opt/root`。

最后一项 scoped 测试只属于 Full/Embedded，因此增量复验 GNU Full/Embedded、MSVC Full、Linux Full/ASan，并清除覆盖计数后重新采集。Boot/Lite 不编译该文件，使用其已通过的原组合。首轮 696/695 的报告保留为 first-pass，不替代最后结果。Linux 最终 206 项编译输入前后摘要一致，见 `out/network-opt/linux/verification-summary.json`；tidy 时点的生产文件也与最终逐项一致。

固定 formatter 为 19.1.5。首轮 PATH 误选 Keil 自带 formatter，工具查询版本崩溃；显式指定 `XGEN_CLANG_FORMAT` 到固定 venv 后检查通过，不修改格式规则绕过失败。

文档和格式日志位于 `out/network-opt/root/{docs,mkdocs,docs-qa,pre-commit}.log`；零拷贝双语示例的严格 C11 编译记录为 `out/network-opt/tx/docs-verify.log`。双语统计表在独立审查中按实际 helper 修正，拒绝增加 `rx_dropped`，不额外虚记 `rx_errors`。3 项 CTest 发现策略检查通过，最终种子 15 的 network 属性测试全部通过。

## Cortex-M0 最终 ELF

Windows 与固定官方 Linux Arm GCC 15.2.1 的 MTU/RX 128、64 × 非 LTO/LTO 共八组合全部通过现有独立预算。两端数字完全一致，Linux LTO 构建契约另有 4/4 通过；100 项生产输入的双系统前后四份快照一致，未使用预算覆盖。见 `out/network-opt/resource/comparison.json`。

| 指标 | 上轮 | 本轮 |
| --- | ---: | ---: |
| 非 LTO Flash | 16,172 B | 16,168 B |
| LTO Flash | 12,796 B | 12,704 B |
| MTU 128 工作区 | 1,112 B | 1,112 B |
| MTU 64 工作区 | 920 B | 920 B |
| MTU 128 RAM 含 1 KiB 预留栈 | 2,200 B | 2,200 B |
| MTU 64 RAM 含 1 KiB 预留栈 | 2,008 B | 2,008 B |
| 非 LTO 最大已链接函数栈 | 192 B | 192 B |
| LTO 最大已链接函数栈 | 216 B | 208 B |

最终报告位于 `out/network-opt/resource/final`。没有放宽预算、添加常驻缓存或新增堆依赖。LTO Flash 相对 12,800 B 预算余量为 96 B；8 KiB Flash 的更小 Boot 目标仍未达到。

非 LTO network 目录归属为 1,244 B：发送 510、接收 176、入口 174、路由 384 B，已包含 route。与上轮 1,278 B 相比少 34 B；新增路由校验自身有成本，不能只计算删除代码的收益。raw 接收仍存在于库源码，当前实际消费者不再经过它，链接器将其裁剪。

LTO 后正常 network RX 入口与 view 交付合并为单函数 72 B 栈；上轮为入口 120 + view 72 = 192 B 的同时存活前缀，本轮少 120 B。非 LTO 前缀保持 8 + 72 = 80 B。这里均未包含上游 parser、下游 transport、应用或 ISR，不代表整条调用链峰值。

ARM32 的私有 frame RX 消息从 12 B 缩为 4 B。network context、route table、路由项、端点以及工作区布局保持不变，单路由 network 工作区归属仍为 84 B，已经包含在总工作区中。

扩展数组去清零的独立对象实验得到 Boot/Full 发送翻译单元各少 12 B `.text`，但发送函数栈仍为 184/384 B。Full 的数字仅来自单个 ARM `-Os` 对象，不能当作 Full 产品最终 ELF 或栈上界。对象级证据位于 `out/network-opt/tx/final`，目标 ABI 布局证据位于 `out/network-opt/resource/layout`。

## 保留边界

公开配置布局和 wire v3 不变；无效/重复静态路由提前拒绝属于行为收紧。私有路由仍保留兼容字段和更新操作，不增加动态路由、入口 ACL、广播泛洪、自动路径 MTU 或独立仅接收链路配置。正常 endpoint 改为必需 view，raw 解码继续独立可用。

未推送、未合并 main、未创建发布 tag，也未执行真实板级、DMA、密码 provider 或 ISR 栈验收。此处本地证据不替代远端 CI 和产品验收。
