# 运行边界与资源证据完善

日期：2026-10-02。本轮接续 [可选运行时实施](system-refinement.md)，起点为 `40cc70c`。只报告本地执行；不替代远端 CI、板级时延、真实密码 provider 或 Bootloader 验收。

## 复审范围和行为

在现有有界运行、scope 所有权和安装消费结构上检查异常输入、绝对期限、混合维护工作及资源证据。未增加 profile、通用组件副本、线程或隐式分配。

Transport 修复三处边界：到期 TX 忙期在新 DATA 交付前终止；有界维护按 peer 轮转处理该 peer 的重传和消息尾部，防止全局消息泵阶段先耗尽全部配额；已缓存并 SACK 的乱序片段从实际接纳起计入重组期限，进入顺序重组时继承同一消息最早的接纳时刻。时钟回绕、多个消息 ID 和精确 scope 隔离分别覆盖。

未分片乱序包不新增消息期限；扩展关闭时不增加相应时间字段。回调串行化、wire v3 和公开配置 ABI 保持。工作配额仍只限制尝试次数，不声明完整 step 的 CPU 周期上界。

ELF 报告生成器现在拒绝缺失、损坏或无有限上界的已链接函数栈记录。错误保存在 `stack_evidence_errors`，生成步骤返回失败，最大栈值为 `null`，后续预算检查不会把部分测量当作有效最大值。合法零栈和 `dynamic,bounded` 记录可接受；被链接器删除的函数不参与判断。源码摘要增加 `dev/` 装配和嵌套 `CMakeLists.txt`，避免构建输入变化不进入摘要。

## TDD 检查点

| 行为 | RED | GREEN |
| --- | --- | --- |
| 缺失/无界栈记录及不完整输入摘要 | `41b4842`：8 项中 6 项真实失败 | `542fcb1`：8 项通过 |
| 部分栈证据不能发布数字最大值 | `33b6919`：真实最大值错误为 8 | `61b5aa9`：9 项全部通过 |
| DATA 截止、混合工作饥饿、乱序首片期限 | `686f5f1`：3 项构建成功后真实失败 | `42592d3`：含原 advanced 组 23/23 |
| 缓存转移、回绕、消息与 scope 隔离 | `9f7a0d0`：新增 2 项真实失败 | `42592d3`：transport 全组 309/309 |

本轮证据根目录为 `out/final-hardening/`。`footprint/red.log`、`partial-red.log`、`green-final.log` 保存工具阶段；`transport/red-tests.*` 与 `cached-red-tests.*` 保存生产行为 RED，`green-tests.*`、`history-tests.*` 保存阶段 GREEN。旧预算和布局检查另有 8/8、6/6 通过，不放宽资源限额。

## 不产生源码变更的审查

Parser 在同一固定种子 `5785420` 下，对 2,500 组噪声、CRC 错误、删插字节、截断、嵌套帧及 26..512 B cache 的输入运行有界、无限配额与扩展关闭路径。交付摘要均为 `7041455143832374297`。异常 PHY 夸大长度或返回 BUSY 的独立探针也保持旧缓存与时间，后续尾部只交付一次。证据在 `parser/`，这两项独立审查探针不计入正式协议 CTest 数量，也不声称是完整模糊测试证明。

API/安全的独立 Full advanced ON 筛选为 221/221，清单、标签与 JUnit 一致，运行时输入摘要稳定。`api/summary.json` 记录该阶段；最终矩阵独立计数。

可选明文模式先接纳乱序数据、再安装同 scope 安全关联的探针确认：安装不撤销此前拥有的数据，它仍可在之后交付。公开接口未承诺追溯认证或清空 transport，因此不通过丢弃已 SACK 数据改变行为。本轮补齐双语安全文档与 API 注释：新的业务信任边界使用新可信 scope，先安装后接纳，明确处置旧数据。证据为 `api/probe.log`、`probe.xml`。

## 最终验证

最终生产修改为 `42592d3`，公开注释随 `bb02a75` 冻结；此后只收尾文档与 CI 接入。Windows GNU 八组及 MSVC 全部通过：

| Profile | OFF 协议 | ON 协议 | 每种模式辅助 | OFF / ON 合计 |
| --- | ---: | ---: | ---: | ---: |
| Full | 813 | 859 | 7 | 820 / 866 |
| Boot | 220 | 243 | 5 | 225 / 248 |
| Embedded Lite | 311 | 350 | 6 | 317 / 356 |
| Embedded | 813 | 859 | 7 | 820 / 866 |

本轮显式设置 `XGL_BUILD_EXAMPLES=ON`，Boot 的 5 个辅助项含工作区、安装消费者及 echo/file-transfer/boot-update 三个示例；上轮 Boot 辅助项只有 2 个，因此不能把合计增加全部当作新增协议用例。

MSVC Full advanced ON 为 **866/866（859 协议 + 7 辅助）**，协议名称与 GNU Full ON 一致，保留 510 个历史名称，无重复、跳过或漏执行；真实 Ninja 头文件依赖正常。六仓编译/测试输入摘要前后相同，证据为 `windows-final/summary.json`、`input-before.json`、`input-after.json` 及各组清单/XML。

Linux Full advanced ON **866/866**；ASan/UBSan **865/865**，启用泄漏检查和错误即停。sanitizer 只排除安装消费者 smoke，该项由普通 Full 验证；没有把排除项计为通过。全部历史测试名称保留，无失败、跳过或重复；54 项 property 标签和种子 `5785420` 已核对。

生产行覆盖率 **4,615/4,916（93.877%）**、函数 **344/344（100%）**、分支 **3,074/3,662（83.943%）**。Clang-Tidy 19.1.0 检查 43 个生产单元通过。构建前、构建后与全部检查后的编译输入清单一致，SHA-256 为 `e418ed9b0c706647dbdce55a48fe38704ed320768ba2f02a84cf67a71ac568ce`，HEAD 均为 `bb02a75`。证据为 `linux/final/verification-summary.json` 及其 `evidence/`；该清单范围与 ARM 生产摘要不同，不能直接比较摘要值。

Cppcheck 2.21.0 的 43 个生产单元、固定格式、文本、全仓 pre-commit、严格 Doxygen、双语 QA 和严格 MkDocs 均通过；root 质量 JSON 另存 `quality/`。Cppcheck 与 Clang-Tidy 的共享编译数据库在不同时段生成，最终 tidy 的 43 个输入均为 Linux 构建路径。历史通过与本轮最终通过分开保存；测试编译或诊断失败也保留。

## Cortex-M0 资源

固定 ARM GNU 15.2.1、MinSizeRel、通用 Cortex-M0 探针，十项均通过禁堆、未解析符号和新栈证据检查。共同输入摘要为 `067791459b1be3dd869cb9db4544415fa56410b5bed4441634164350f1578b04`，构建前后相同；汇总、身份及 ELF 对照在 `arm/final-summary.json`、`arm/final-source-identity.json`、`arm/elf-comparison.json`。

| 组合 | Flash 普通 / LTO | workspace MTU128 / 64 | RAM 含预留栈 MTU128 / 64 |
| --- | ---: | ---: | ---: |
| default，扩展 OFF | 16,060 / 12,752 B | 1,072 / 880 B | 2,160 / 1,968 B |
| small，扩展 OFF | 15,828 / 12,580 B | 1,040 / 848 B | 2,128 / 1,936 B |

八份默认 ELF 的已分配文件段与上一轮同配置逐字节一致，八组原预算全部通过。默认 LTO 仍有 48 B 余量。

| 扩展 Boot，MTU128，LTO | Flash | 相比上一轮 | workspace | RAM 含预留栈 |
| --- | ---: | ---: | ---: | ---: |
| 宏 ON，旧入口探针 | 14,052 B | +64 B | 1,128 B | 2,216 B |
| 宏 ON，真实扩展 API 探针 | 14,496 B | +72 B | 1,128 B | 2,224 B |

这些扩展探针单独量测，不套用默认预算。全部 RAM 总量包含 1,024 B 栈预留；最大单函数栈普通/LTO 为 192/208 B，扩展为 216 B，均不变。Flash 增长如实记录，不称为同功能体积缩减。

Full advanced 的乱序节点只增加首次接纳时间，实际 target 编译量测如下；节点 `sizeof` 与工作区对齐后的每槽预留分别记录：

| ABI / 工具链 | 节点 sizeof 前 / 后 | allocator alignment | 每槽预留前 / 后 |
| --- | ---: | ---: | ---: |
| Cortex-M0 / ARM GNU | 52 / 56 B | 8 B | 56 / 56 B |
| Win64 / GNU | 80 / 88 B | 16 B | 80 / 96 B |

因此该字段不增加所测 ARM 配置的节点槽预留；所测 Win64 配置增加 `16 × max_rx_buffered_packets` B。证据在 `arm/node-layout/summary.json`，使用真实编译的尺寸/对齐及现有 workspace 分区规则计算，并未声称执行了 ARM API 或测量了所有 ABI。扩展关闭及 Boot 无此字段。

## 验收边界

栈检查只证明存在有效的编译器记录，不证明每个已链接汇编/运行库函数都有记录，也不求完整调用链或 ISR 嵌套峰值。RAM 中的栈仍是链接器预留。摘要范围扩大后不能将新旧摘要差异解读为固件字节发生变化，应比较同配置 ELF。

产品仍需确定真实 Boot 分区、驱动和回调的栈、DMA 一致性、唤醒与容量恢复机制、可靠传输的业务确认及重启密钥新鲜性。本轮不推送、不把本地成功写成远端 CI 或硬件通过。
