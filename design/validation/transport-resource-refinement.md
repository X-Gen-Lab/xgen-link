# Transport 事实归属、准入与资源裁剪验证

实施日期：2026-10-02。行为基线 `45aa76a`，最后生产逻辑冻结点 `725a4fd`；`ae59a0f` 只移动两条已有局部诊断注释，最后构建/测试冻结点为 `4bdc046`。后者修复 Lite 的示例能力选择。架构与兼容边界见 [ADR 0012](../adr/0012-transport-resource-contract.md)。文档和 CI 随后提交不改变编译输入；原始证据保留在忽略的 `out/transport-next/`，本文提供可审阅摘要，不宣称这些本地产物已经远端发布。

## TDD 检查点

| 提交 | 实际 RED/GREEN 与范围 |
| --- | --- |
| `cf3848b` | 本地准入、HELLO 预约、重组可靠事实及拥有对象删除的真实 RED；资源选择契约首先因缺少能力/接口编译失败 |
| `ad677f1` | 已知拥有记录删除 4/4 GREEN；拒绝其他队列的存活对象，保留独立多目标接口 |
| `0af1e1b` | 用旧生产库运行固定 RTO 回归，4 项中 1 RED、3 护栏通过 |
| `2d8b3f3` | footprint 格式 6 与目标实编资源身份的工具 RED |
| `d8ca799` | 全局容量压力下，失败 scope 与已拥有消息错误优先级的 2 项 RED |
| `b79abf4` | 预准入、固定租期、消息可靠属性、编译资源选择的最小 GREEN |
| `2843ce4` | 使用其他资源组合的报告套用预算，真实 RED |
| `1e1bb7c` | RX 私有职责收敛、元数据/插入位置复用、双端集成和预算身份拒绝 GREEN |
| `725a4fd` | 四个 TX 回滚点复用已知拥有记录，229/229 相关回归通过 |
| `ae59a0f` | Clang-Tidy 发现已有 NOLINT 位于声明行而非宏展开行；只移动注释后同一 43 个生产翻译单元 GREEN，例外范围不扩大 |
| `de2d26c` | 完整 GNU Lite 矩阵 257/258，转发示例退出 3；新增真实四 profile 构建契约仅在 Lite 注册多余示例时 RED |
| `4bdc046` | 按实际转发能力选择示例；全部 5 项 profile 契约 GREEN，覆盖四组合例子运行、Debug/Release 安装与能力消费 |
| `4af91d8` | 历史 RX chunk 的格式 5 fixture 被格式 6 解码器拒绝：6/7 通过、1 ERROR；仅迁移该工具 fixture 后 7/7 GREEN，不改变生产 C、GoogleTest 或构建输入 |

普通可靠发送在 HELLO 前预留本地记录与载荷。分片可靠发送预留完整消息副本和首片资源，不预分配未来所有片。后续背压持有已接受数据；首次 HELLO/DATA 提交失败释放相应本地预留，对端空 HELLO 预约由独立 5 秒期限回收。

8 项 fragment scope 回归覆盖属性混用、乱序 SACK 前拒绝、ACK 失败后的接受事实、不同 scope 同时过期和非可靠超时保护可靠历史。24 项 TX contract 与 4 项双端集成直接驱动真实生产协议；fixed-RTO 的 4 项 GoogleTest 从已安装定制 SDK 链接并按 `unit;xgl;transport;fixed-rto` 逐项发现。

原始 RED/GREEN 位于 `root/`、`rx/`、`tx/`、`resource/`。首轮脚本引号错误、静态诊断失败、预算失败和编译中途输入变化均保留，不将它们改写为最终通过；最终批次使用独立目录并核对源输入前后一致。

## 主机、消费与质量

最终矩阵在 `4bdc046` 重建后执行；各平台、profile 与 sanitizer 分别记录，不能用其中之一替代其他组合。保留全部 510 个历史协议测试名，没有删除回归或禁用测试换取通过。Lite 原错误转发示例按实际不具备的编译能力排除，仍构建并运行其三个适用示例；Full/Embedded 保留多节点示例。

| 组合 | 最终结果 |
| --- | --- |
| Windows GNU Full / Embedded | 各 778/778 CTest，含 771 个 GoogleTest；runner 各 6/6 |
| Windows GNU Boot / Lite | 174/174、257/257 CTest；GoogleTest 分别为 169、251 项 |
| Windows MSVC Full / Boot | 778/778、174/174；Full runner 6/6，严格 C11/C++17 |
| Linux GNU Full | 778/778，全部示例和 smoke 一并执行 |
| Linux ASan + UBSan | 777/777，缺少的唯一项为独立 SDK consumer，由普通配置执行 |
| Linux 生产覆盖率 | 行 4063/4335（93.7%）、函数 303/303（100%）、分支 2684/3208（83.7%） |
| Cppcheck / Clang-Tidy | 固定 2.21.0 / 19.1.0，各 43 个生产 C 翻译单元通过 |
| 配置/安装消费 | dependency 11/11、profile 5/5、transport resources 6/6、RX chunk 7/7；fixed-RTO SDK 另执行 4/4 GoogleTest |
| 文本/格式/文档 | 固定 formatter、pre-commit、严格 API Doxygen、MkDocs 与双语 QA 通过 |

Windows 六组合的共享质量 gate 均通过，266 项输入前后一致，清单摘要为 `6211075640d8519d013699d38c9c4cfa132ea522f58510e7bc4384f67e11c8be`。每组合 29 个公开、私有及生成头独立以 C11/C++17 首包含编译；discovery/JUnit 名称、标签和结果逐项一致，无失败、跳过、禁用或重复。证据为 `out/transport-next/windows-final-4bdc046/summary.json`。

性质测试默认基础种子为 `0x58474C`（5785420），各测试记录基础种子与独立随机流到 JUnit。runner 的环境/命令行覆盖与失败回放契约实际执行，回放使用记录种子和原测试过滤条件，不把“随机通过一次”当作确定性证明。

Linux 直接 CTest 和随后两个共享 gate 分别归档，前后输入一致；从清空旧 gcda 后的完整测试重新采集覆盖率。证据为 `out/transport-next/linux/final-4bdc046/{verification-summary,gate-summary}.json` 及其 JUnit、discovery、coverage/tidy 报告。前轮增量 discovery 的旧 gcda timestamp mismatch、首轮输入中途变化各自保留，未混入最终计数。

`4af91d8` 只修改 Python 工具测试的布局 fixture，完整 7 项 RX chunk 契约重新运行；未改 `4bdc046` 已验收的生产/GoogleTest/CMake 输入。此前 `725a4fd` 和 `ae59a0f` 的数据仍为独立历史批次，不冒用最终冻结证明。MkDocs 首次误用未安装该工具的质量 venv，随后改用已有固定文档环境；新链接的两项 strict 警告也修复后重新通过，原失败日志保留。

开发准备、发现策略、footprint budget/storage 的独立工具契约分别为 11/11、3/3、5/5、6/6。新资源契约用真实配置、生产窗口、安装 target/头/运行时消费者检查各边界，版本和来源由显式路径提供；配置、构建和测试不联网选择依赖。

## Cortex-M0 最终 ELF

固定 Arm GCC 15.2.1，Cortex-M0 `-Os`，Boot、一个 peer、运行时窗口 1、一个可靠记录、一条链路/路由、PHY chunk32。普通库构建与显式库 LTO 分开；布局和消费者保持非 LTO，防止空测量回调使真实协议接收路径消失。MTU/RX 容量为 128 或 64。

| 指标 | 默认 MTU128 | 默认 MTU64 | small MTU128 | small MTU64 |
| --- | ---: | ---: | ---: | ---: |
| 编译窗口容量 | 128 | 128 | 1 | 1 |
| 索引桶数 / 自适应 RTO | 1 / 开 | 1 / 开 | 1 / 关 | 1 / 关 |
| 资源身份 | 1857 | 1857 | 1025 | 1025 |
| 非 LTO Flash | 16,336 B | 16,336 B | 16,100 B | 16,100 B |
| LTO Flash | 12,964 B | 12,964 B | 12,788 B | 12,788 B |
| 工作区 | 1,072 B | 880 B | 1,040 B | 848 B |
| 静态 RAM | 1,136 B | 944 B | 1,104 B | 912 B |
| 加 1 KiB 预留栈的 RAM | 2,160 B | 1,968 B | 2,128 B | 1,936 B |

Flash 为最终完整探针 ELF 的装入内容；静态 RAM 包括工作区，不再重复相加。预留栈为预算项，未被探针运行测量为真实峰值。默认 Boot peer 为 108 B，池对齐到 112 B；small peer 为 80 B，池块 80 B，每 peer 工作区减少 32 B。重组对象仍为 176 B，新增可靠属性使用原 padding。

默认非 LTO 两组通过旧 16,384 B 门槛；默认 LTO 两组 **失败，超旧 12,800 B 门槛 164 B**。small 四组通过各自精确绑定的独立政策，LTO 仅余 **12 B**。原默认限制不放宽，CI 保留全部八组合中的默认 LTO 任务，不使用 small 替代默认报告，不通过排除任务或吞错取得绿色状态。

small 的显式选项是 `XGL_TRANSPORT_WINDOW_CAPACITY=1`、`XGL_RELIABLE_INDEX_BUCKETS=1`、`XGL_ENABLE_ADAPTIVE_RTO=OFF`；其生成构建身份为 66561。默认生成身份仍为 301。两者均 chunk32、无 fallback；实例窗口同为 1，但编译能力不同。

对本轮默认，small 的普通/LTO Flash 减少 236/176 B，工作区减少 32 B。相对 `45aa76a` 的相同默认选择，Flash 增加 220/172 B，工作区增加 8 B；不能把新 small 对旧默认的较小值称为同功能优化。

最后四处已知记录回滚使首轮普通/LTO Flash 减少 40/28 B。首轮 small LTO 12,816 B 的失败和默认 LTO 12,992 B 的失败保留。之后 `ae59a0f` 的八份完整 ELF 与 `725a4fd` 对应文件 SHA-256 及字节完全相同，诊断注释没有改变二进制。

最后 `4bdc046` 八组再次正式链接、提取布局及反汇编，与 `ae59a0f` 的八份完整 ELF 逐字一致。协议及五基础组件的探针编译输入前后相同；扩展构建规则与工具测试的变化另外记录，未混成固件差异。最终证据为 `out/transport-next/arm/freeze-4bdc0463/{summary,precise-policy-audits,comment-binary-comparison,final-source-binding,stack-paths,readonly-symbol-costs}.json`。

八组均无禁堆符号和未解析符号。默认/small 的预算均核对实际容量、桶数、自适应 RTO、资源身份、MTU、chunk 和 LTO；跨组合、缺失字段和旧格式不能被当作合格报告。

## CPU、结构与未达到的约束

顺序分片元数据不重复解码；OOO 查找返回可复用的插入位置。ACK_RANGE 先验证全部声明，再按有序记录和降序区间用单游标确认；SACK 先验证全部位图声明，再遍历实际拥有记录。ACK/TX rollback 复用已知记录，避免重新按身份查找。

fragment 的维护过期检测为单次列表遍历，但终止可靠 scope 时仍需扫描清理同 scope 对象，因此不声称整个超时维护严格为 O(R)。peer 查找、容量计数及部分生命周期扫描仍为有界线性；缓存计数、轮转和配额需要产品 WCET/公平性测量后另立契约，不为猜测收益增加状态。

最终默认 LTO 相对旧默认的 172 B 增量可由符号表对齐：新增回收期限 helper 96 B，发送入口 +60 B，接收入口 +52 B，拥有记录删除 +34 B，unindex +10 B，其余增减抵消。正确性、所有权和 HELLO 排空保护不能为达到门槛而删除。尚未量测的 TX 测量合并、内部 index 遍历精简只是候选，不据估计收益宣称预算通过。

从 `xgl_step()` 起的已知普通 ACK 路径为非 LTO 988 B、LTO 844 B；延迟重传为 564/580 B，最大已链接单函数栈为 192/208 B。消费者 `Reset_Handler` 自栈 96 B，实际 probe RX 返回空。接入真实 RX 后，同一非 LTO 调用者加普通 ACK 已至少 1,084 B，LTO 为 940 B，均尚未含实际驱动、应用回调和 ISR。1 KiB 预留不构成栈安全证明。

本轮没有达到 8 KiB Flash 的小 Boot 目标；MTU64 工作区低于 1 KiB，MTU128 尚未达到。源码与工程规范是本地基线，未推送、发布 tag 或证明远端 CI 全绿。真实板卡、DMA 生命周期、生产密码 provider、断电和实时性由产品独立验收。
