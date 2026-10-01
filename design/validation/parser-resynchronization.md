# Parser 缓存重新同步验证

日期：2026-10-01。起点 `fb53b99`；决策见 [ADR 0009](../adr/0009-parser-resynchronization.md)。本记录区分行为、静态资源、主机耗时和未完成的板级验收。

## TDD 与行为

| 检查点 | 实际证据 |
| --- | --- |
| `dafd2d1` parser RED | 缺失一个、两个 CRC 字节的两项新测试因缺少 advance 接口编译失败；此前真实生产探针已复现后帧无法交付 |
| `3bcb1c0` parser GREEN | 两项新测试通过，新增共享边界检查、缓存候选选择和逐帧消费接口 |
| `4e0c3c1` 接收 RED | 新增 6 例，实际 4 失败、2 通过；旧 SDK 和新 parser/旧 receive 两种装配均复现缓存帧丢失 |
| `233fec6` 接收 GREEN | 6/6；先排空缓存再接收输入，保留首个上层错误，认证或 BUSY 拒绝不扫描载荷 |
| `61ec605` 边界补齐 | parser 共 16 例，覆盖完整候选优先、半帧保留、时钟回绕、容量及扩展指针；完整 GoogleTest 661/661 |
| `3131b68` 消费契约 RED | 无后续 magic 的坏 CRC 候选要求延迟到 advance 再释放；旧实现直接 reset，状态与缓存长度两项断言失败 |
| `301bf80` 编码保护 | 全零/全 FF 存储编码结果一致，头内每字节被覆盖，容量外哨兵保持；纯重构保护在修改前通过 |
| `048ad10` 最终 GREEN | 统一 RECOVER、消除重复检查/清零和接收分支；7 条旧私有调用按新契约迁移，完整 GoogleTest 663/663 |

最终新增 17 项 parser、6 项接收及 1 项头编码测试，均调用真实生产实现。认证拒绝接收用例用模拟上层返回值验证帧消费边界，不替代 provider 验签验证。历史 `AfterReset` 用例显式调用 reset，与新的私有 feed/advance 契约一致；名称和 CRC 分类断言保留。总 RECOVER 状态消除拒绝时的预扫描，空缓存恢复在 advance 中统一结束。

RED 证据位于 `out/parser-resync-tests/red-build.log`、`out/parser-resync-analysis`、`out/datalink-resync-rx/red-evidence.json`。旧 SDK 副本有 archive 摘要，避免用已修改实现冒充旧行为。其余回归与真实生产调用没有被替换为测试算法。

## 首轮验证

对应 `61ec605` 的生产/测试基线：Windows GNU Full、Embedded 各 668/668，Boot 117/117、Embedded Lite 152/152；MSVC Full 668/668、Boot 120/120，后者额外启用三个示例。两套 Full runner 契约各 6/6。Windows GNU 矩阵及 MSVC 输入哈希前后一致。

Cppcheck 2.21.0 的 43 个生产源文件零诊断。GoogleTest 为 1.16.0、严格 C++17；独立 C11/C++17 头编译、安装消费及原有 510 个名称继续执行。首轮产物分别位于 `out/resync-root/final`、`out/resync-msvc`、`out/resync-quality/cppcheck`。

正式精简后在 `048ad10` 上重新执行。Windows GNU Full / Embedded 各 **670/670**，Boot **119/119**，Embedded Lite **154/154**。其中 Full 的 663 项 GoogleTest 加 7 项 smoke/示例；Boot 为 117 项 GoogleTest 加 2 项 smoke。首轮报告归档到对应 `first-pass`，最终结果使用原路径，未复用旧源码的绿色结果。

| 最终检查 | 本地结果 |
| --- | --- |
| MSVC Full / Boot | 670/670、122/122；Full runner 6/6；202 个输入文件前后哈希一致 |
| Linux GNU Full | 670/670，包含独立安装消费 |
| Linux ASan / UBSan | 669/669，无跳过；独立安装消费者由普通 Full 验证 |
| 生产覆盖率 | 行 3,848/4,170（92.3%）、函数 290/291（99.7%）、分支 2,450/2,990（81.9%） |
| 静态分析 | Cppcheck 2.21.0、Clang-Tidy 19.1.0 全部通过，43 个生产 C 文件；编译路径指向正式 `src` |
| 格式与文档 | 全仓 pre-commit、clang-format 19.1.5、严格 Doxygen/MkDocs、双语文档 QA 通过 |

覆盖率包含完整生产范围，三项均超过 80%，未排除难测文件。Linux 的 `out/resync-root/linux/evidence` 保存命令、源码摘要、报告与退出码；最终四个 test/coverage/tidy gate 均为 0。Doxygen 通过共享质量入口运行，站点与文档 QA 分别记录命令。

## 资源审计与取舍

首版 Cortex-M0 / Arm GNU 15.2.1 / `-Os` / Boot / chunk32：非 LTO Flash 16,288 B，LTO 12,912 B；相对 `fb53b99` 分别增加 300 B、312 B。非 LTO 通过 16,384 B 上限，但 LTO 超出 12,800 B 上限 **112 B**。失败报告保留在 `out/resync-resource/final`，不会记作全部预算通过。

两种 MTU 的常驻工作区保持 1,112 B / 920 B，静态 RAM 保持 1,176 B / 984 B；加 1,024 B 预留栈后分别为 2,200 B / 2,008 B。parser 仍为 24 B，每 PHY link 仍为 40 B，没有第二份接收缓存或堆服务。首版非 LTO/LTO 最大单函数静态栈为 192 B / 216 B；它不是完整调用链高水位。

精简候选在 ignored 构建目录试验，选择后才写入生产源：共享固定头字段规则，删除全字节覆盖前的冗余清零；HEADER 阶段先证明基本帧长度可容纳，EXTENSIONS 只比较标签与剩余容量，避免溢出与重复验证；正文和 CRC 共用完整长度；错误统一进入 RECOVER；接收循环仅有一处 advance 调用。没有删除完整 CRC、认证结构或容量检查。

正式生产源的最终 Windows 四组合如下，构建缓存已核对不含试验 override。最终产物位于 `out/resync-resource/final2`，首版失败留在 `final`。

| MTU/RX | LTO | Flash | workspace | 静态 RAM | 含 1,024 B 栈 RAM | 最大单函数栈 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 128 | OFF | 16,172 B | 1,112 B | 1,176 B | 2,200 B | 192 B |
| 64 | OFF | 16,172 B | 920 B | 984 B | 2,008 B | 192 B |
| 128 | ON | 12,796 B | 1,112 B | 1,176 B | 2,200 B | 216 B |
| 64 | ON | 12,796 B | 920 B | 984 B | 2,008 B | 216 B |

相对恢复前的 `fb53b99`，Flash 分别增加 **184 B / 196 B**，常驻 RAM 不变。原有四份预算均未改动；LTO 只剩 **4 B** 余量，不承诺不同编译器版本保持该结果。ARM32 分别实编 Boot/Full 的 parser24 / link40，见 `layout-final2/results.json`。

固定官方 Linux Arm 工具链的四组合也逐项通过同一预算，与 Windows 四组指标相同；102 个生产输入逐文件 SHA 一致，见 `source-comparison-final2.json`。Linux 矩阵的编译与四项预算命令均为 0，外层脚本最后因测量期间更新了 `tools/boot_footprint/README.md` 而退出 3。before/after 唯一变化是该非编译文档，四份报告的生产源码摘要相同；保留原退出码，并单独核对真实编译输入，不把外层命令记录为成功，也不为文档变化重复编译未变的 ELF。

最大单函数栈不变或下降不等于整条调用链下降。非 LTO `.su` 与反汇编确认 feed32、inspect80、advance40；普通解码调用前缀 step40 + poll80 + span136 + feed32 + inspect80 + wire96 + ext72 共 **536 B**，恢复路径将 feed32 换成 advance40，共 **544 B**。这些真实 BL 调用仍持有父栈，不能作为尾调用抵消；数字还没包含更深的 CRC、应用外层和 ISR。故不能据此宣称 512 B 栈足够，1,024 B 仍只是探针预留。

## 执行时间边界

恢复只在当前候选失败后搜索；正常载荷不参与重新同步。缓存内找到完整帧时同步交付，随后继续排空，无需新增 PHY 输入。原始错误只记录一次，扫描中的假候选不重复增加 CRC 计数。

magic 搜索、候选校验与缓存压缩是不同工作。合法头 CRC 的重叠长候选可能让最终帧 CRC 检查重复覆盖同一批字节；多帧排空还会执行上层回调。因此固定缓存确保内存有界，但不能把 RX 新读字节预算解释为 CPU 周期上界。主机性能脚本位于 `out/resync-resource/performance`，其耗时不证明真实 MCU 的实时性。

最终合成输入实测保存在 `performance/final2`：64 / 128 / 2,048 B 缓存的坏外帧分别恢复 1 / 3 / 77 个完整帧，各只报告一次原始错误。长候选输入分别包含 1 / 4 / 84 个合法头 CRC 候选，最后字节及全部排空的 PC 中位耗时约 0.32 / 1.04 / 147.45 μs；Full 密集假 magic 约 27.93 μs，正常帧最后 CRC 约 3.24 μs。它们是并行构建环境中的观察值，不含产品回调开销，不是 MCU 周期、吞吐或最坏时间保证。

## 验收范围

wire v3、公开 ABI、profile ID、五个独立基础组件和同步 PHY 所有权保持。新接口为私有 parser 契约；正常应用继续通过 `xgl_step` 接收。

未增加超时恢复策略。恢复后缀保守继承原期限，可能早于其真实到达时间过期。CRC 不能证明发送者可信，也不能保证任意损坏流无歧义恢复。

资源探针不含真实 BSP、DMA、Flash 驱动、看门狗、升级业务及生产密码 provider。8 KiB Flash 目标和板级最坏周期、调用链/ISR 栈峰值仍需产品验证。本轮没有推送、合并 main、创建 tag 或执行远端 CI。
