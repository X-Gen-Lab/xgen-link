# Transport 准入、恢复与存储优化验证

实施日期：2026-10-01，验证收尾：2026-10-02。实施前基线 `e1c6649`，首轮冻结点 `45d54bd`，最终生产及测试冻结点 `45aa76a`。架构决定见 [ADR 0011](../adr/0011-transport-admission-recovery.md)。本记录区分最终 ELF、对象布局、主机验证与尚未执行的板级验收。

## TDD 与行为变化

| 检查点 | 内容与证据 |
| --- | --- |
| `c6e2fc9` | 44 项专项首轮：RX 13/16、TX 6/8、runtime 9/9、storage 5/11 按预期失败；其余 11 项为既有行为护栏 |
| `157d2f6` | 格式 5 / 窗口池移除的工具契约 4 项，3 项 RED |
| `44008f8` | 延迟恢复与维护错误返回，runtime 9/9 GREEN |
| `d2c8686` | RX 语义准入、反馈错误返回，16/16 GREEN |
| `7d3c53e` | TX 初次失败回收和可靠广播拒绝，Full/Boot 各 8/8 GREEN |
| `0c7d9f3` | 内嵌窗口、连续乱序存储和单一重组长度；37 项 storage/fragment/window 回归通过，工具 4/4 与预算工具 5/5 |
| `45d54bd` | 精简 TX plan、收紧 ACK 临时数组、私有重传实现、迁移历史同步反馈断言；补充完成消息背压后终止失败的唯一释放检查 |
| `1c8a34f` | 交叉审查补充缓存交付后 ACK BUSY 和后续硬交付错误：2 RED，另有 1 项跨 peer 护栏 GREEN |
| `2ad8671` | 同次缓存交付先 ACK BUSY、后 ACK TX_FAILED 的真实 RED |
| `45aa76a` | 区分保留数据的应用背压和不保留数据的反馈失败；同级首错、硬错优先，Full 746/746 GREEN |

原始 RED/GREEN 位于 `out/transport-opt/{root,rx,tx,storage}`。历史测试名称不删除：ACK/SACK 后显式调用维护再检查后续发送；无效 DATA fixture 补齐真实 wire 必需的类型；终止超时和 PHY 错误断言改为实际错误返回。旧的直接调用重传 helper 测试改为真实 SACK 输入加维护。四项 timeout 测试保留名称，覆盖真实发送时间、边界、回绕与零拷贝，不再仅检查配置赋值。

交叉审查的失败与最终通过日志/JUnit 位于 `out/transport-opt/review`。最后四项只在支持乱序的 profile 启用；首轮 Linux/Windows 结果单独归档，后续记录不冒用旧覆盖率或将失败改写为通过。

公开发送/step 接口和 wire v3 保持；可靠广播、保留类型、重复 FRAGMENT、耗尽包号是明确拒绝。已经接受的数据不会因 ACK 发送失败回滚。重复 SACK 保留暂时背压 deadline，ACK/close 可取消尚未发送的重试。快速重传只标记，后续分片由维护泵送；就绪工作发布零延迟。

## 最终主机与工具结果

| 组合 | 结果 |
| --- | --- |
| Windows GNU Full / Embedded | 各 746/746 CTest，含 739 个 GoogleTest；runner 各 6/6 |
| Windows GNU Boot / Embedded Lite | 155/155、222/222 CTest |
| Windows MSVC Full / Boot | 746/746、158/158；Full runner 6/6；Boot 多启用三个示例 |
| 配置与消费契约 | 首轮 4/4，含关闭 libc fallback、Debug/Release 多配置安装、导出能力与拒绝不支持能力；最后修复不改变构建/导出配置，最终各 profile 再次通过 SDK 消费 CTest |
| 工具契约 | footprint storage 4/4、budget 5/5、CTest 策略 3/3 |
| Cppcheck | 2.21.0，43 个生产 C 翻译单元通过 |
| Linux GNU Full / ASan+UBSan | 746/746、745/745，无失败、跳过或禁用；sanitizer 关闭独立 SDK 消费测试 |
| Linux 生产覆盖率 | 行 3952/4223（93.6%）、函数 293/293（100%）、分支 2603/3110（83.7%） |
| Clang-Tidy | 19.1.0，43 个生产 C 翻译单元通过 |
| 格式与文档 | 全仓 pre-commit、固定 formatter、文本、严格 Doxygen/MkDocs 和双语 QA 通过 |

Windows GNU 编译输入前后逐项一致，命令/JUnit 位于 `out/transport-opt/root/final`；MSVC 编译前后快照与摘要位于 `out/transport-opt/msvc/summary.json`。两个完整 profile 保留 510 个历史名称；全部公开、私有及生成头继续独立 C11/C++17 首包含编译。MSVC 生产编译启用 `/W4 /WX`。

首轮 `45d54bd` 的 Linux Full/ASan 为 742/741 项，覆盖率为 93.5%/100%/83.6%，保留在 `out/transport-opt/linux/first-pass-45d54bd`。最终批次存放在 `final-pass-45aa76a`：增量构建后的测试发现读取旧 gcda 时曾报告 timestamp mismatch，构建退出零；随后明确删除 43 份旧覆盖计数，从完整测试重新采集，上表没有混用旧计数。

Linux 最终总表为 `out/transport-opt/linux/final-pass-45aa76a/verification-summary.json`；195 项构建输入前后完全一致，清单摘要为 `58b79c2424824a4346d3a0b305fd8d7172594c7b01a642f8ae83eab59d1ce2a3`。固定种子 5785420 可回放，首轮测试全部保留且仅新增四个 Buffered 回归。sanitizer 缺少的唯一检查是独立 SDK consumer，由普通配置执行。格式与文档日志位于 `out/transport-opt/root/*final.log`。

## Cortex-M0 最终 ELF 与布局

固定 Arm GCC 15.2.1、Cortex-M0 `-Os`，MTU/RX 容量 128 或 64，PHY 每次读取 chunk 仍为 32。Windows/Linux 八份最终 ELF 全部通过既有独立预算，四组 Flash 镜像跨平台逐字节一致；与首轮 `45d54bd` 的镜像也分别相同。最终比较、17 条成功 Linux 命令及 LTO/storage/budget 契约 4/4、4/4、5/5 记录在 `out/transport-opt/resource/late-fix/comparison.json`，原批次保留。

100 项编译输入在 Windows/Linux 各自前后与最终源码之间一致，规范化清单摘要为 `b9424ab16b5c40870465898084b68eaecc5fe85544d07da8ea9878f6bc25941a`。测量时文档尚未提交，报告如实标记 dirty。报告自身使用平台 `Path` 排序，Windows/Linux 大小写排序不同，原始 `source_sha256` 因而不同；比较脚本已从相同文件精确复现两种值，未把不同摘要误称相同或忽略输入变化。

| 指标 | 前轮 network | 本轮 transport | 变化 |
| --- | ---: | ---: | ---: |
| 非 LTO Flash | 16,168 B | 16,116 B | −52 B |
| LTO Flash | 12,704 B | 12,792 B | +88 B |
| MTU 128 工作区 | 1,112 B | 1,064 B | −48 B |
| MTU 64 工作区 | 920 B | 872 B | −48 B |
| MTU 128 静态 RAM | 1,176 B | 1,128 B | −48 B |
| MTU 64 静态 RAM | 984 B | 936 B | −48 B |
| MTU 128 RAM 加 1 KiB 预留栈 | 2,200 B | 2,152 B | −48 B |
| MTU 64 RAM 加 1 KiB 预留栈 | 2,008 B | 1,960 B | −48 B |
| 非 LTO / LTO 最大已链接单函数栈 | 192 / 208 B | 192 / 208 B | 不变 |

Flash 与 RAM 都是完整探针 ELF 的口径。LTO Flash 增加是修正行为与编译优化共同作用后的实际成本；12,800 B 回归限额只剩 **8 B** 余量，不能称有充足裕量。普通限额 16,384 B 余量 268 B。未放宽预算，未新增堆服务或未解析符号。8 KiB Flash 的更小 Boot 目标仍未达到。

布局由目标 ABI 实编探针取得，见 `out/transport-opt/resource/layouts/layouts.json`：

| ARM32 私有结构 | 当前尺寸与意义 |
| --- | --- |
| window | 28 B，含 16 B 确认位，最多 128 槽 |
| Boot peer | 104 B，原 96 B；增长 8 B 与移除独立 8 B 窗口块抵消 |
| reliable record | 72 B 不变；快速重传标志使用原有 padding |
| OOO node | 原 64 B → 52 B；池对齐后 56 B，载荷/TLV 共用一块存储 |
| reassembly | 184 B → 176 B，区间槽数不变 |
| Boot 管理前缀 | 512 B → 464 B，移除分配服务和池描述产生 48 B 净节省 |
| Lite / Full 管理前缀 | 1,224 / 3,168 B → 1,136 / 3,080 B，各少 88 B |

这些 Full/Lite 结构值不是产品总 RAM。工作区同时受 peer、TX、乱序与消息容量影响；最终产品需用自己的配置查询并链接。公开报告 `window_bytes=0`，内嵌部分计入 peer，不能重复扣除窗口池。格式 5 显式要求 `window_pool_blocks=0`，旧格式不能被默默当作新布局。

## 调用栈边界

依据最终 ELF 的调用边与 `.su` 核算，以下为从 `xgl_step()` 起的已知同步路径，不含外部调用者、真实驱动/应用回调或 ISR：

| 路径 | 非 LTO | LTO |
| --- | ---: | ---: |
| 可靠接收后同步发送普通 ACK，至帧头 CRC | 988 B | 844 B |
| 维护阶段执行延迟重传，至帧头 CRC | 564 B | 580 B |

旧的 SACK 接收直接进入重传的 1,052 B 非 LTO 路径已经断开。普通 ACK 路径只从旧 1,004 B 降到 988 B，仍然很接近 1 KiB；这里的“最大单函数 192 B”不能替代整条路径累加。

每个函数的 `.su`、最终指令地址和直接/端点间接调用边保存在 `out/transport-opt/resource/late-fix/stack-paths.json` 及其 `linux/stack-paths.json`，比较报告再次核对两平台及首轮/最终版本的路径一致性。

探针的 `Reset_Handler` 自栈为 96 B，实际 `probe_rx` 返回空，ELF 没有测量真实接收运行峰值。若产品采用相同调用者并接入真实 RX，仅普通 ACK 路径加该调用者就为非 LTO 1,084 B、LTO 940 B，尚未计入中断与未知回调。因此 1 KiB 仍是资源预留项，不能据此宣称板级栈已安全。产品需按完整调用链配置栈，并通过栈高水位、边界与中断场景验收。

## 交付边界

本轮保留既有分层文件职责、窗口能力、RTT/RTO 和有界索引容量，不引入运行时组件发现或另一套轻量协议。源码改动本地提交；远端 CI、发布 tag、真实板卡、DMA、生产密码 provider 与断电场景没有由这些主机测试代替。
