# 独立组件与协议重构实施记录

更新日期：2026-10-02。当前阶段为 transport 准入、恢复与存储优化。各阶段证据分别记录；历史结果不代表当前提交已经重跑。原阶段对应[历史实施计划](REFACTORING_PLAN.md)，依赖决策见 [ADR 0001](design/adr/0001-core-dependency.md)，结构决策见 [ADR 0005](design/adr/0005-protocol-structure.md)，当前决策见 [ADR 0011](design/adr/0011-transport-admission-recovery.md)。

## 当前：Transport 准入、恢复与存储优化

完整 RX 语义检查在 peer/缓存准入前完成，拒绝保留类型、可靠广播、重复 FRAGMENT 和耗尽包号。初次 TX 失败只回收本次新建且未使用的 scope。接受后的 ACK 发送错误原样返回，保留已经接受的历史，避免重复交付。

ACK/SACK 接收仅处理确认与标记，快速重传及窗口释放后的分片发送交给维护阶段。就绪工作发布零延迟，重复反馈保留 PHY 背压重试期限。维护返回第一个终止性错误并继续其他 peer；临时背压仍持有数据。窗口位内嵌，乱序载荷/TLV 共用存储块，重组仅保留一个消息长度，删除多余分配服务和池描述；wire v3、公开函数及配置布局不变。

Windows GNU Full/Embedded 各 746 项、Boot 155 项、Lite 222 项通过。多配置安装/能力契约首轮 4 项通过，完整配置 runner 保留 510 个历史测试名。生产和测试最终冻结点为 `45aa76a`；交叉审查新增四项回归，修复维护误把 ACK 失败当成保留数据的背压，硬错误不再被此前临时反馈失败掩盖。详细 TDD、MSVC/Linux/静态质量结果与最终资源证据汇总见[传输优化验证](design/validation/transport-optimization.md)。

最终 MSVC Full/Boot 为 746/158 项，Linux Full/ASan+UBSan 为 746/745 项；生产行/函数/分支覆盖率为 93.6%/100%/83.7%。Cppcheck、Clang-Tidy 各 43 个生产文件及格式、pre-commit、严格 API/站点文档通过。Windows/Linux 八组 ARM ELF 通过原预算，各组 Flash 镜像逐字节一致；远端 CI 和真实板级验收另行记录。

Boot MTU128/64 工作区为 1,064/872 B，各减 48 B。非 LTO Flash 为 16,116 B，LTO 为 12,792 B，后者比前轮增加 88 B 且仅余 8 B 回归预算。同步 SACK 重传链已断开；普通接收后 ACK 的已知栈链仍为非 LTO 988 B/LTO 844 B，不含调用者、驱动回调和 ISR，1 KiB 预留不等于产品栈验收。未放宽预算；8 KiB Flash 的小 Boot 目标仍未达到。

## 历史：Network 契约与资源优化

静态配置提前拒绝目标零、本机目标和重复目标；整表加载先检查再替换。工作区规划与链路初始化共用 PHY 去重规则，并按唯一 PHY 合并轮询周期。保留兼容配置布局，metric 不参与择优，私有路由更新不改变链路生命周期。

内部帧 RX 端点只携带必需的已验证 view；raw 解码入口独立保留并检查实例帧长。转发参数从 view 获取、失败透传原始错误并释放 scratch。零拷贝区分基础请求拒绝和实际逻辑提交，普通/原地/scoped 发送的错误统计一致。删除无生产用途的错误包装，扩展数组保持容量并取消多余清零。

新增跨 profile 边界、路由装配、统计和真实三节点可靠/认证/未知 TLV/广播/MTU/BUSY 测试。GNU Full/Embedded、MSVC Full、Linux Full 各 697 项通过，GNU Boot/Lite 为 128/169 项，MSVC Boot 为 131 项，ASan/UBSan 为 696 项。覆盖率行/函数/分支为 92.7%/99.7%/82.7%；Cppcheck 与 Clang-Tidy 的 43 个生产 C 文件通过。

Windows/Linux 八组 ARM 预算全部通过且生产输入一致：非 LTO/LTO Flash 为 16,168/12,704 B；MTU128/64 工作区仍为 1,112/920 B，含 1 KiB 栈预留 RAM 为 2,200/2,008 B。LTO 正常 network 接收栈前缀从 192 B 减到 72 B，不代表完整调用链峰值。具体命令、TDD 检查点、接口边界和未上板限制见 [network 验证记录](design/validation/network-optimization.md)。

## 历史：CRC 错误后的缓存重新同步

错误只拒绝当前候选，缓存由私有 advance 入口继续寻找有效帧头；完整帧优先，无完整帧时保留最早可能半帧，包括尾部单个 `A5`。接收层同步排空多个缓存帧，仅释放已交付帧，返回首个上层错误。正常载荷内的 magic 不抢占候选，BUSY/认证拒绝不触发载荷重新同步。借用 view、固定容量和绝对期限均显式约定。

17 项 parser、6 项接收恢复及 1 项完整头覆盖测试保留真实生产验证；历史私有调用迁移到 ERROR/COMPLETE 后 advance，原有测试名称不变。固定头验证共用规则，完整编码覆盖全部字节，去掉无必要的缓冲区清零；不更改 wire v3 或公开 ABI。

最终结果与首版超出 LTO 预算的失败证据分别见[缓存重新同步验证](design/validation/parser-resynchronization.md)。既有 Flash/RAM 预算不放宽；没有额外常驻缓存或动态内存。本阶段仅本地提交，不推送、合并 main 或创建 tag。

## 历史：datalink 有界接收与资源优化

在 `d7bbb75` 基础上修复重叠 magic 候选期限和固定头超容量拒绝；将兼容 view 局部变量移出生产 feed 热路径。删除 parser 冗余 index，头长度采用 wire 原生范围；合并 metadata 包装，poll 通过同步 span 统一组帧和交付。四个实际单向接口改为独立 TX/RX 类型，实例静态绑定，公开 ABI 与 wire v3 保持。

受控 `XGL_DATALINK_RX_CHUNK_SIZE` 支持 AUTO、16、32、64、128；AUTO 的 Boot 为 32，其余为 128。安装头拒绝宏冲突，资源报告与预算核对目标编译的真实 chunk。Boot 旧 smoke 的单次完整 ACK 假设在真实失败后修复，BUSY、重传、窗口复用与回绕断言保留。

GNU Full/Embedded 各 646 项、Boot 95 项、Lite 130 项，MSVC Full 646 项、Boot 含示例 98 项通过；原 510 名称保留。28 个头分别通过 C11/C++17。Linux Full 646 项、ASan/UBSan 645 项通过；生产行/函数/分支覆盖率为 92.2%/99.7%/81.8%，43 个生产 C 文件的 Cppcheck 与 Clang-Tidy 通过。新增配置、安装和报告契约 7 项及其余开发消费契约通过，Boot 显式 chunk16/64/128 的安装和工作区 smoke 也通过。

Windows 与固定官方 Linux ARM 四组合结果一致：普通/LTO Flash 为 **15,988/12,600 B**，最大单函数栈 **192/224 B**；MTU128/64 workspace **1,112/920 B**，含 1 KiB 预留栈 RAM **2,200/2,008 B**。相对前轮普通 Flash 增加 56 B、LTO 不变，工作区各减少 24 B。RAM 与栈预算已收紧，无堆和未解析符号；8 KiB Flash 目标仍未达到，未上板。逐项证据和诊断修正见[验证记录](design/validation/datalink-optimization.md)。

## 历史：wire 布局、借用视图与体积优化

在 `0e08bdf` 基础上统一 checked 长度和发送布局，修复辅助长度运算回绕；移除未使用的私有字段，集中扩展长度常量，零拷贝 helper 合入帧编码文件。SACK 改为借用视图，头 CRC 使用独立 CRC 组件的增量接口，完整接收直接解码到输出头并保持失败清零。生产源从 45 个归并为 44 个，未改变 wire v3 或公开 ABI，也未新增常驻工作区。

Windows GNU Full/Embedded 各 630 项、Boot 79 项、Lite 114 项，MSVC Full 630 项、Boot 含示例 82 项通过。510 个历史测试名完整保留，29 个头分别通过 C11/C++17；Linux Full 630 项、ASan/UBSan 629 项及生产覆盖率行 92.2%、函数 99.6%、分支 81.7% 通过。静态分析、固定格式和严格 API 文档通过；各自来源和诊断配置修正见[本轮验证记录](design/validation/wire-optimization.md)。

最终 Cortex-M0 普通 ELF 为 **15,932 B Flash**（原 16,060 B）；显式启用 LTO 为 **12,600 B**，最大单函数栈分别为 272/312 B。MTU128/64 workspace 仍为 1,136/944 B，含预留 1 KiB 栈的 RAM 为 2,224/2,032 B。Windows 与固定官方 Linux 工具链的四组合结果一致，无堆和未解析符号，通过各自独立预算；原 LTO 实验 12,472 B 不替代本轮实际值。pre-commit、严格站点构建和双语 QA 通过。CI 已增加 LTO 模式，尚未声明远端运行或板级验收通过，8 KiB Flash 目标仍未达到。

## 历史：有界恢复、调度与资源优化

在 `af9799a` 基础上修复重复 SACK 快重传、首次 RX 准入后的空 peer 占用、RTT 中间值溢出、成功发送统计和认证广播拒绝。parser 的超时维护独立于轮询和 RX 字节预算，step 返回首个错误并继续处理已读数据及可靠维护；升级示例同步处理可恢复 BUSY。

增加兼容的 scoped zero-copy、纯布局查询和精确工作区分类报告。security 持有唯一认证策略；移除 link、packet 和层上下文的冗余字段，合并 transport TX 私有头。Boot 逻辑发送扩展缓冲缩为 32 B，wire 解码能力不缩减。新增 Embedded Lite 304；原 profile 身份、旧公开结构 ABI 和 wire v3 保持。

| 本轮本地验收 | 实际结果 |
| --- | --- |
| Windows GNU Full / Embedded、MSVC Full、Linux GNU Full | 各 613/613 CTest；606 个 GoogleTest 加 7 项 smoke/示例 |
| GNU Boot / Embedded Lite、Linux Lite | 66/66（64 GoogleTest）/ 101/101（98 GoogleTest）/ 101/101；含安装消费 |
| GNU 与 MSVC 构建契约 | Boot、Lite、Debug/Release、安装能力四类通过；MSVC Boot 最终 66/66 |
| Linux ASan / UBSan | 612/612，含泄漏检查；安装消费另由 Full 验证 |
| 生产覆盖率 | 行 3794/4124（92.0%）、函数 281/282（99.6%）、分支 2407/2954（81.5%） |
| 边界与辅助契约 | 28 个头各 C11/C++17；11 项依赖、11 项开发准备、3 项发现策略、5 项资源门禁；三个主机 benchmark 执行通过 |
| 质量与文档 | 45 个生产翻译单元的 Cppcheck/Clang-Tidy、严格 Doxygen/MkDocs、全仓 pre-commit、双语 QA 通过 |

Boot/Lite 也通过固定 shared test runner 的清单与 JUnit 核验。完整证据见[本轮验证记录](design/validation/protocol-hardening.md)，Linux 汇总位于 `out/opt-linux/verification-summary.json`。

同工具链 Cortex-M0、MTU/RX 128 的完整 ELF：Flash **15,692 → 16,060 B**，workspace **1,168 → 1,136 B**，静态 RAM **1,232 → 1,200 B**，含 1 KiB 预留栈的 RAM **2,256 → 2,224 B**；最大已链接单函数栈 **392 → 272 B**。Flash 增加 368 B，未将功能修复的成本掩盖为缩减。

MTU/RX 64 首次测量为 Flash **16,060 B**、workspace **944 B**、静态 RAM **1,008 B**、含预留栈的 RAM **2,032 B**，最大单函数栈 **272 B**。两组无堆服务或未解析符号，通过正式回归限额；仅 64 的 workspace 达到 1 KiB 目标，两组 Flash 仍未达到 8 KiB 目标。未执行板级验收或本轮远端 CI。

新增固定工具链 ARM CI、四 profile 测试与安装能力验证；ARM 安装脚本在本地 Linux 原样完成下载、校验、解包，两组实际 ELF 与预算结果和 Windows 一致。TDD 检查点、命令和实际产物见[本轮验证记录](design/validation/protocol-hardening.md)。未合并 main 或创建 tag。

## 历史：协议结构与资源优化

在 `b158d60` 基础上实施，生产源从 63 个归并为 45 个，transport 从 34 个归并为 17 个。私有头按所属层放到 `src`，测试按层组织，CMake 拆分源码、安装和开发辅助职责，继续只导出一个协议 target。普通、认证、原地及转发发送共享规划和 PHY 提交；接收传递已验证借用视图，不重复完整 CRC。

实例拥有唯一安全上下文，每唯一 PHY 只预留一个 parser/cache。已修复 SACK/BUSY 确认顺序、发送预检、重组消息预算、跨度溢出、原地容量检查及流式 CRC 分类；RTT 与 workspace 预留统计补齐。Boot 编译裁剪统计及详细错误文本，保留公开错误契约。wire v3 和公开结构 ABI 不变。

| 本轮本地验收 | 结果 |
| --- | --- |
| Windows GNU Full / Embedded，MSVC Full，Linux GNU Full | 各 582/582 CTest；575 个 GoogleTest、7 个 smoke/示例 |
| GNU Boot / MSVC Boot | 2/2（含安装消费）/ 严格构建与静态工作区 smoke 通过 |
| Linux ASan / UBSan | 581/581，含泄漏检查；安装消费另由普通 Full 验证 |
| 生产覆盖率 | 行 3665/3988（91.9%）、函数 269/270（99.6%）、分支 2308/2842（81.2%） |
| 分析范围 | 45 个生产翻译单元；覆盖报告另包含 4 个有执行代码的私有头 |
| 编译与依赖契约 | 29 个头分别 C11/C++17；11 项生产依赖、11 项开发准备、3 项发现策略、6 项 runner 契约通过 |
| 质量与文档 | 固定 pre-commit、format、text、Cppcheck、Clang-Tidy、严格 Doxygen/MkDocs 与双语 QA 通过 |

同工具链重跑的 Cortex-M0 完整 ELF：Flash **16,764 → 15,692 B**，workspace **1,424 → 1,168 B**，静态 RAM **1,488 → 1,232 B**，加预留 1 KiB 栈后的 RAM **2,512 → 2,256 B**；最大已链接单函数栈仍为 392 B。无堆服务或未解析符号。满足探针的 64 KiB / 8 KiB 限额，仍未达到 8 KiB Flash / 1 KiB workspace 目标，未执行板级验收。

真实 RED/GREEN 检查点、工具和产物路径见[协议结构验证记录](design/validation/protocol-structure.md)。本节记录本地证据；远端结果以此次推送提交对应的 Actions 为准，不沿用下面历史 CI。现有 `.spec-workflow/` 保留在工作区，未提交；不合并 main 或创建 tag。

## 历史：独立交付与质量补齐

六个独立仓库已正常推送 main，远端 Windows/Linux CI 全部通过：

| 仓库 | 固定提交 | 成功运行 |
| --- | --- | --- |
| quality | `ddd42d4b7eb8c8b744a019015d6ad412f78c10fe` | [36805023273](https://github.com/X-Gen-Lab/xgen-quality/actions/runs/36805023273) |
| crc | `0d286f40899ba3479a9f0ae13e399d75dab331b3` | [36805057780](https://github.com/X-Gen-Lab/xgen-crc/actions/runs/36805057780) |
| bytes | `76b8cca9418d9f16914c61f95e34d6c48ae46c56` | [36805063355](https://github.com/X-Gen-Lab/xgen-bytes/actions/runs/36805063355) |
| status | `58919927dfb5aedd305cc972485e126a7cdb17c5` | [36805067382](https://github.com/X-Gen-Lab/xgen-status/actions/runs/36805067382) |
| memory | `353e4b4f66316bb55a1ef2b52ad64e5e64aa3d9d` | [36805072414](https://github.com/X-Gen-Lab/xgen-memory/actions/runs/36805072414) |
| containers | `f727a11304786a1bf7be1a57ec5dafca16f0cfb2` | [36805076834](https://github.com/X-Gen-Lab/xgen-containers/actions/runs/36805076834) |

上述运行实际完成测试、安装消费和适用质量检查，Linux artifacts 已下载并核对 GitHub SHA256。组件报告的源码提交与干净工作区一致；quality 的 Windows 路径别名测试已修复。link 的开发清单和质量工具固定至这些提交。

link 已实现严格 C++17、逐 GoogleTest 发现/标签、510 个历史名称逐一保留、可回放种子，以及共享 text/format/test/cppcheck/tidy/docs/coverage 入口。真实新增回归修复了 ACK 长度乘加溢出和初始化失败后重复归还 RX 存储。协议三项覆盖率门槛均为 80%，只统计完整生产源码集合。

完整重构快照为 `c13d9b9`；最终生产与测试修复为 `1b0ce7eb658ec43891a78630a773c822d7f9c595`，已推送 `feat/independent-memory-containers`。基础包继续 main，link 未合并 main 或创建 tag。用户原有 IDE 删除及 `.spec-workflow` 模板未混入协议提交。

| 本轮验收 | 实际结果 | 来源 |
| --- | --- | --- |
| 远端全新克隆 GNU Full / Embedded | 各 561/561；554 个 GoogleTest 加 7 项 smoke/示例 | 干净 `1b0ce7e`，独立克隆五依赖和 quality；`build/delivery-completion/remote-link/build/remote-validation-1b0ce7e.json` |
| 全新克隆 Boot | 2/2；静态工作区与安装包消费者 | 同一干净提交，各配置使用独立构建目录 |
| 安装消费 | Full / Embedded / Boot 都实际安装、find_package、编译并执行 | 各自 `xgl_sdk_consumer_smoke`；没有读取原工作区未跟踪文件 |
| 头文件独立编译 | 21 个源码头分别首包含；MSVC 取得 RED 后通过 | `test/cmake/HeaderContracts.cmake`；生成配置头另由公开头间接包含 |
| Windows 本地 MSVC | 561/561；runner 契约 6/6 | `build/delivery-completion/msvc-quality-test.json`，实际 Developer Shell 内执行 |
| 开发输入 / 生产依赖 / CTest 策略 | 11/11、11/11、3/3 | 生产契约 `build/delivery-completion/contracts-final/run-pxmrmljl/result.json`；策略分别验证 CTest 3.31 和 4.2 |
| 本地生产覆盖率 | 行 3672/4011（91.5%）、函数 249/250（99.6%）、分支 2315/2867（80.7%） | `build/coverage-agent-full/complete-summary.json`；63 个生产 C 文件与报告集合一致，后续干净 CI 另记 |
| 本地质量 | text、format、pre-commit、Cppcheck、Clang-Tidy、严格 Doxygen / MkDocs 通过 | 固定工具与 shared runner；Linux GNU 11.4/Clang-Tidy 19.1.0，Cppcheck 2.21.0 |

`1b0ce7e` 的[远端完整矩阵](https://github.com/X-Gen-Lab/xgen-link/actions/runs/36807629994)全部通过，九个 jobs 均成功。Linux、Windows/MSVC 和 macOS Host 各执行 561 项，ASan/UBSan 执行 560 项（独立安装消费者由普通构建验证），CI Boot/Embedded 分别执行 2/3 项。完整 GoogleTest Embedded 矩阵由上述全新克隆另行执行，不混淆两个配置的数量。

远端生产覆盖率为行 **3671/4010（91.5%）**、函数 **249/250（99.6%）**、分支 **2314/2865（80.8%）**，三项均达到 80%。报告包含全部 63 个生产 C 文件及一个内部 inline 头；Cppcheck 和 Clang-Tidy 各分析 63 个翻译单元通过，严格 API/站点 Doxygen 告警日志均为空，发布验证与 11 项依赖契约通过。八份 artifacts 已核对 GitHub SHA256；全部质量报告来源为同一干净提交。可复核汇总、原始元数据和审计脚本保存于 `out/reports/remote-ci/36807629994/`。

本轮 Cortex-M0/GCC 15.2.1 `-Os`、无 LTO 在干净 `1b0ce7e` 重新链接：Flash 16,764 B、静态 RAM 1,488 B、其中 workspace 1,424 B，额外预留栈 1,024 B，合计 RAM 2,512 B，最大已链接单函数栈 392 B，无堆服务或未解析符号。结果与原基线一致，满足通用 64 KiB/8 KiB 探针上限；不满足独立的 8 KiB Flash/1 KiB workspace 设计目标，也不表示真实板级 Boot 已完成。

真实回归检查点包括：种子/发现/C++17 的 `753cc0c`；GNU 插桩 `a9ba4d8` → `bb1d7f5`；ACK 溢出 `fc04cb3` → `19707db`；失败初始化所有权 `e188f3b` → `ee195c2`；独立头首包含 `4ebb339` → `db6900f`；CTest 策略 `325d521` → `1b0ce7e`。首次远端运行也保留失败日志，后续修复不将它改写为通过。局部静态分析依据和工具配置例外见[规范采用](docs/standards.md)。

## 历史：正式模块与开发装配分离

link 的生产入口只消费父工程已经提供的兼容 targets 或通过 `find_package` 取得的安装包。产品决定每个镜像的唯一源码组合；协议模块不再维护五个 `external` 生产子模块，也不下载、搜索或选择基础组件源码。所有入口继续校验版本范围、ABI、target 类型和同包版本一致性。

独立开发使用 [dev/CMakeLists.txt](dev/CMakeLists.txt) 和五个显式 `XGL_DEV_*_SOURCE_DIR`。旧 `XGL_*_SOURCE_DIR` 生产选项被移除，配置时给出迁移错误；不能将旧路径悄悄当作新默认。`dev/dependencies.json` 是 link 开发与 CI 的固定测试输入，不是产品依赖锁文件。开发入口主动开启必要的测试辅助，正式模块作为子目录时默认关闭示例、smoke 和发布辅助。

| 本轮验收 | 结果 | 实际证据 |
| --- | --- | --- |
| 生产预提供 targets / 安装 package | 10/10 契约检查通过 | `build/dependency-ownership/contracts-green/run-n83hm6jq/result.json`；真实消费者及缺包、错误版本/ABI/类型、同包混版本拒绝 |
| 旧源码选项拒绝与 dev 显式装配 | 本地通过 | 契约检查有真实 RED/GREEN，五个旧选项均拒绝；Full、Embedded、Boot 使用显式 dev 装配 |
| 子目录默认隔离 | 本地通过 | 同一契约矩阵验证默认不添加示例、smoke 或发布辅助，完整父工程 targets 可直接消费 |
| Full / GCC | 8/8 CTest，510 个 GoogleTest 通过 | `build/dev-integration-full` |
| Embedded / GCC | 4/4 CTest，510 个 GoogleTest 通过 | `build/ownership-embedded` |
| Boot / GCC | 2/2 CTest 通过 | `build/ownership-boot`；静态工作区与安装消费者 |
| ARM / 显式 dev 源码 | 重新链接通过，资源与前轮相同 | `build/ownership-arm/boot-footprint.json`；无堆服务和未解析符号 |
| dev 输入准备工具 | 11/11 单测通过 | prepare 脚本行 56/57（98.25%）、分支 27/28（96.43%）；不代表协议覆盖率 |
| 文档与快速质量 | 本地通过 | dev 文档构建的严格 MkDocs、Doxygen；固定 clang-format 19.1.5 和全仓 pre-commit |
| MSVC / Linux sanitizer | 本轮未重跑 | 不沿用前一五包阶段的 MSVC 结果或仅配置的 Linux CI |
| 远端 CI / 发布 / 硬件 | 未执行 | 不能根据本地配置或历史验收推断完成 |

修改前 Full 基线另重跑 8/8 CTest；生产契约 RED 与 GREEN 日志分别保存在 `build/dependency-ownership/contracts-red-full` 和 `contracts-green`。五个 `external` 子模块已移除，当前无 `.gitmodules`；同级独立仓库保持原状。当前源码组合及已有脏状态按实际报告记录，不宣称它已经是干净的远端发布组合。

本轮 Cortex-M0 / GCC 15.2.1 / `-Os` / 无 LTO：Flash 16,764 B，静态 RAM 1,488 B（包含 workspace 1,424 B），预留栈 1,024 B，合计 RAM 2,512 B，最大已链接单函数栈 392 B。此次显式 dev 源码重跑与前轮数值一致；相比原 core 基线仍增加 552 B Flash 与 8 B RAM，不宣称小 Boot 设计目标已达成。

本轮 RED/GREEN、命令、产物及执行边界见[依赖所有权验证记录](design/validation/dependency-ownership.md)。dev 文档构建产物位于 `build/ownership-docs/link/docs/site`；CI 静态复审和本地通过不等于远端执行。

## 历史：五包迁移与空行规范阶段

本节至“双仓阶段”之前记录此前五个子模块的迁移、格式和资源证据。文中的入口、提交及“本轮”均属于该阶段；五子模块不再是现行生产接入方式。

| 归属 | 当前契约 |
| --- | --- |
| xgen-status | `xgs::status`，通用状态；strings 可选，协议不自动引入 |
| xgen-memory | allocator、pool、size_class、arena、tracking、libc_allocator 六项；tiered 收敛到一个 size_class 实现 |
| xgen-containers | list、hash、bitset、ring_buffer 四项；DMA 模型与硬件适配验收分开 |
| xgen-bytes | `xgb::bytes`，基础整数读写 |
| xgen-crc | CRC8/CRC16 独立 targets；协议消费 `xgcrc::crc16` |
| xgen-link | 协议状态、wire、datalink、network、transport、security、API 及资源规划 |

五包独立版本为 0.1.0，消费范围 `>=0.1.0,<0.2.0`、ABI 1。公开分配器变为 `xgm_allocator_t`，容器/字节/CRC 使用所属公开前缀。通过父工程兼容 targets、五个显式 `XGL_*_SOURCE_DIR`、已准备的 `external/xgen-<模块>` 或安装包接入；不再消费 core 聚合包。详细入口见 [迁移指南](docs/zh/guide/modular-migration.md)。

当时五个实际 Git 子模块固定以下本地已验证提交；`.gitmodules` 使用 `../xgen-<模块>.git` 相对地址，本地 `.git/config` 覆盖为已验证仓库，新远端尚未创建或推送。这是已退出的阶段安排，当前准备方式见[构建与测试](docs/zh/getting-started/build-and-test.md)。

| 子模块 | 功能迁移验证基线 | 当前空行规范提交 |
| --- | --- | --- |
| status | `28bc7be369b81e9b9b9d7379743dd0da6fa4647b` | `8899670e35efe55ede9f578f700d3182ad058af1` |
| bytes | `960038992b30ac9c54bef190e9acabe86239f8fc` | `457cca4e53c02147625ea599a7fd5c78a7716891` |
| CRC | `ec4c73d21771c57f3abfb33fc686d2d9b9585288` | `085c0691c7ca17e3b0b404153b516600be202730` |
| memory | `1360e489ca97a594a47968572d6b6c833aac532f` | `3288afcac2a26a59ac65ac5f13aa358b73e7c190` |
| containers | `c2997e0b500ef64af282961b0d471ba8b79d9954` | `80e34031d660dd122c3a367f6c639009de8b44ce` |

当前提交增加空行配置、有限公共头检查的工具来源和采用记录；源码只有空行及 LF 调整。下文功能、覆盖率和 ARM 结果仍归属功能迁移基线，不标为在格式提交上重新执行。格式阶段的独立结果见 [空行规范验收](design/validation/blank-line-layout.md)。

## 本轮基线与本地验证

| 验证 | 实际结果 | 状态说明 |
| --- | --- | --- |
| 来源 core | 11/11 CTest | 本轮在旧来源实际重跑，保持历史行为输入 |
| 迁移前 link | 8/8 CTest | 对已有协议工作区重跑，不重置其改动 |
| ACK RED | 4 项中 2 项失败、2 项通过；提交 `b169a10` | 128 个标志实际申请 128 字节而非 16 字节；初始化失败修改 allocator 字段 |
| 新五包 Full / GCC | 最终 8/8 CTest，510/510 GoogleTest | 中间检查点 `6927196` 为 507 项；最终集合含后续回归 |
| Full / MSVC | 8/8 CTest，510/510 GoogleTest | 真实 MSVC 环境执行，公开头对齐类型问题已修复 |
| Embedded / GCC | 4/4 CTest，510/510 GoogleTest | 生产 fallback 关闭，主机夹具显式提供 allocator |
| Boot / 默认子模块 | 全新构建 2/2 CTest | 静态工作区及安装消费者 |
| CompactWindow | 4/4 通过 | 相同 RED/GREEN 行为回归 |
| Benchmarks | Full/Embedded 的三个 benchmark 均构建并执行通过 | Embedded 生产 fallback 关闭；测试显式链接 host libc allocator，主机执行不代表 MCU 吞吐实测 |
| memory / GCC 与 MSVC | 各 23/23 CTest | 22 组单元及 1 项集成，内含 15 种消费场景；行 99.3%、函数 100%、分支 93.2% |
| containers / GCC 与 MSVC | 各 33/33 CTest | 16 个 GoogleTest、17 个消费测试；行 99.4%、函数 100%、分支 98.3% |
| 依赖入口 | 三入口真实 C 消费成功；最终 65 个错误或缺包场景拒绝 | 完整预提供集兼容，partial 集合保留身份检查；同包混合补丁版本另有两路径 RED/GREEN |
| 纯安装上游 SDK smoke | 有效 RED 后 1/1 GREEN | 向隔离消费者传播显式上游 prefix/package 提示 |
| core 最终退出 | `b042d51abf45b411a8bf1fdbd98337270f8d024c`，工作区干净 | 受控文件 33→5，无生产实现或兼容包装；旧 gitlink 移除，历史 Git 对象保留 |
| 远端 CI、发布、真实板级 DMA | 未执行 | 本地文件和模型不构成这些结果 |

TDD 细节见 [独立组件验证记录](design/validation/independent-components.md)。新增 ACK 位图使用真实公共 bitset；协议 PHY 仍要求同步消费或复制，基础 ring_buffer 不将其变成异步零拷贝接口。

上述检查点记录可重放测试及当时工作区结果；生产迁移与此前未提交协议重构同时保留在工作区，不能宣称单独检出检查点就包含全部受测代码。

最终 Full/Embedded 的 XML 位于 `build/independent-migration/full-final.xml`、`msvc-final.xml` 和 `embedded-final.xml`。最终 memory 提交只在受测生产来源 `dace87da` 上补充 pool descriptor/storage 不重叠契约，格式和 Doxygen 已重验；其生产行为没有改变。

### 规范迁移差异

新 memory/containers 使用 C++17 和 `gtest_discover_tests`。link 的历史测试仍使用 C++20，包含已有 property concepts 和测试语法；CTest 保留一个聚合 `xgl_tests` 入口，实际执行 510 个 GoogleTest。本轮没有测量 link 完整 80% 覆盖率，不能将基础组件达标外推给协议。测试语言、细粒度发现和协议覆盖率继续单独治理，不声明所有规范已完全采用。新增 window 行为保留有效 RED/GREEN、非零 head 跨尾 ACK 和完整 128 位窗口推进回归。

### ARM 最终资源结果

本轮 GCC 15.2.1 / Cortex-M0 / `-Os` / 无 LTO 实际链接的最终结果如下。全新 `build/independent-arm-final` 使用默认五个实际子模块，没有源码路径覆盖；报告记录实际路径和干净固定提交。数值与中间结果一致。

| 指标 | 迁移前 | 新组合最终值 | 变化 |
| --- | ---: | ---: | ---: |
| Flash | 16,212 | 16,764 | +552 |
| 静态 RAM | 1,480 | 1,488 | +8 |
| 其中 workspace | 1,416 | 1,424 | +8 |
| 额外预留栈 | 1,024 | 1,024 | 0 |
| RAM 含预留栈 | 2,504 | 2,512 | +8 |
| 最大已链接单函数栈 | 392 | 392 | 0 |

本次最终结果出现资源增长，不能描述为体积优化完成。最终 ELF 无堆服务和未解析符号。workspace 已计入静态 RAM，不重复相加；预留栈不是实际高水位，单函数栈不是完整调用链上界。该值满足测量镜像 64 KiB Flash / 8 KiB RAM 限额，尚未达到 8 KiB Flash / 1 KiB workspace 设计目标。ARM 链接不代表上板或真实 DMA 验收，完整产品仍需对应板卡和驱动条件。

## 历史：2026-09-30 双仓阶段

以下章节及其“当前”“最新”描述均指该历史阶段，包含当时的 core 交付和既有协议重构结果。实现分支为 `refactor/modular-core-boot`；它们不表示五包迁移后的状态。

## 仓库交付

`xgen-core` 已独立提交并推送到 `git@github.com:X-Gen-Lab/xgen-core.git`：

- 分支：`refactor/modular-core-boot`
- 提交：`dc5eb1167ba21de384e7a760a8586b87502b73b2`
- 本地同级仓库与远端一致，工作区干净。
- `xgen-link/external/xgen-core` 是实际 Git 子模块，固定同一提交；`.gitmodules` 使用上述远端。
- 协议仓库当前实现保留在本地分支工作区，尚未提交或推送协议仓库。

默认构建直接使用子模块；也支持父工程提供 targets、显式本地源码和已安装 package。配置过程不会下载 core。要求 core >= 1.0.0、< 2.0.0，ABI 1。

## 最终代码边界

| 归属 | 内容 |
| --- | --- |
| xgen-core | C11 status、带 ctx 的 allocator、fixed/size-class/tiered pool、tracking、intrusive list、hash、bytes、CRC8/CRC16 |
| xgen-link/api | 配置、生命周期、精确工作区规划、显式时间调度、发送、统计、协议错误域、安全会话入口 |
| xgen-link/wire | 唯一帧编码、TLV、CRC 布局、借用 view、流 parser |
| xgen-link/datalink | 每 PHY 独立解析及同步发送 |
| xgen-link/network | 固定容量路由、本地交付、TTL 转发 |
| xgen-link/transport | 唯一 peer 状态、可靠队列、ACK/SACK、重传、乱序保留、分片消息推进 |
| xgen-link/security | 显式可信会话、独立安全序号、nonce/AAD、防重放 |

协议仓库已删除公共 allocator/list/hash/pool/CRC/bytes 包装及重复实现、旧 packet pool、旧通用 void 层接口、network metadata 转发包装、全局时钟/mutex/platform 层、未接回的 codec 注册器。公共工具测试随归属进入 core，协议测试保留真实行为回归。协议错误枚举和路由/packet 语义保留在 link。

## 公共契约

- 使用 `xgl_send_at`、`xgl_send_zerocopy_at`、`xgl_step`、`xgl_next_timeout`，无全局时间服务。
- 同一实例由应用串行访问，回调不得重入或销毁它；同步 PHY 返回前必须消费或复制帧。
- 配置、PHY、provider 和回调上下文借用到销毁；不可变配置可驻 Flash。
- 静态初始化和 allocator 创建共用同一精确布局。动态创建只向后端申请一次工作区，运行时全部使用独立类型资源池；没有耗尽后的堆回退。
- peer、可靠 TX、乱序 RX、重组槽、单消息及共享消息字节预算显式给出；容量溢出在测量前拒绝。
- 路由初始化后不扩容。Boot 使用线性路由，其他 profile 直接使用 core hash。
- peer 身份唯一为 `(remote_id, connection_id, session_epoch)`。只有可靠 DATA 消耗可靠包序号。
- RESET 终止精确 scope 并保持失败终态；显式关闭释放容量，重新连接使用新 epoch。已使用可靠编号的 peer 不会因空闲自动释放再复用编号。
- 接收 BUSY 不误确认；已 SACK 数据保留到有序交付；ACK 范围在完整验证后原子应用。
- wire 仅支持 v3，拒绝 v2 及未实现的加密标志，不自动降级。
- 安全关联只能显式安装；每次签名尝试消耗独立 64 位序号，重传重新签名但保持 DATA 编号。nonce 为可信方向前缀与安全序号，TTL/头 CRC 在 AAD 中规范化。
- 关闭安全关联保留墓碑，不能重新使用同一 nonce 域；跨重启的新鲜性由应用持久化状态或新可信密钥提供。
- 零拷贝入口只支持单帧非可靠发送，同时受全局和路由 MTU 限制。

## 实施阶段

| 阶段 | 当前结果 |
| --- | --- |
| P00 | ADR、scope/所有权、安全版本、可复现资源基线已更新 |
| P01 | scope、ACK、接纳、容量、帧边界及失败恢复回归已落地 |
| P02–P03 | 独立 core、公共组件提取、安装包、远端同步、实际子模块完成；过渡包装已删除 |
| P04 | packet/frame 类型化同步边界、共用 decoder/view 已接入 |
| P05 | 每 PHY parser、显式时钟、预算和模块 timeout 完成 |
| P06 | 唯一 peer 可靠状态、统一 ACK/重传/取消/RESET 路径完成 |
| P07 | 精确类型池、多窗口、分片及认证静态工作区接入同一状态机 |
| P08 | Boot/Embedded/Full 编译裁剪、生成头、ABI 校验及双仓安装消费完成 |
| P09 | wire v3、安全序号、会话安装/关闭、转发与重放语义及回归完成 |
| P10 | 主机 Boot 固定块升级示例、慢 Flash BUSY、丢 ACK 恢复及 Cortex-M0 实际链接完成；真实板级验收未完成 |
| P11 | 有界跨窗口消息推进、静态重组及认证完成；当前明确使用同步 PHY。可选 DMA lease 和后续 codec 不属于已实现能力 |
| P12 | 过渡实现清理、双语文档和本地消费门禁已落地；远端 CI 执行、正式发布和具体产品验收尚未完成 |

这份记录不把无硬件条件下的主机模拟当成全部产品验收，也不把未达到的小 Boot 目标标为完成。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 子模块 core / libc ON | 11/11 CTest，含独立纯 C 安装消费者 |
| 子模块 core / libc OFF | 11/11 CTest，含独立纯 C 安装消费者 |
| Full / GCC | 503/503 协议单元、性质及集成测试；8/8 CTest，包含四个主机示例 |
| Embedded / C11 / libc OFF | 3/3 CTest：静态收发、安装消费者、显式后端无堆 smoke |
| Boot / C11 / libc OFF | 2/2 CTest：静态收发、安装消费者 |
| Boot 加示例 / libc OFF | 5/5 CTest，包含 echo、文件块及 Boot 更新示例 |
| 静态认证双端 / xgl_noheap | 丢 ACK 后 fresh nonce 重传、双向恰好交付一次，独立链接验证通过 |
| cppcheck | 63 个生产 C 源文件，warning/style/performance/portability 检查通过 |
| Doxygen | 公开头文件生成无警告 |
| MkDocs | strict 构建通过 |
| 文档 QA | 双语标题层级及 API/测试引用检查通过 |
| 格式 | 改动 C/C++ 文件按仓库 clang-format；Doxygen 格式保持一致；diff --check 通过 |

测试数与旧 811 项不可直接等同：通用组件、旧平台和失效兼容 API 测试已移出协议仓库，新增回归测试真实协议行为。Linux ASan/UBSan CI 已配置，本次 Windows 执行不冒充 sanitizer 验收。

主要构建目录：`build/clean-full`、`build/clean-embedded`、`build/clean-boot`、`build/boot-example`、`build/submodule-core-on`、`build/submodule-core-off`。

## MCU 资源

最新 Cortex-M0 / GCC 15.2.1 / Thumb / -Os / 无 LTO 实际链接：

| 指标 | 字节 |
| --- | ---: |
| Flash | 16,212 |
| 静态 RAM | 1,480 |
| 其中 workspace | 1,416 |
| 额外预留栈 | 1,024 |
| RAM 含预留栈 | 2,504 |
| 最大已链接单函数栈 | 392 |

配置为单 peer、单路由/PHY、window 1、MTU/RX 128、一个可靠 TX 槽、无认证。最终 ELF 无堆服务及未解析符号。满足测量镜像的 **64 KiB Flash / 8 KiB RAM** 限额；尚未达到 **8 KiB Flash / 1 KiB workspace** 设计目标。

Flash 包含消费者及实际 C 库依赖，RAM 已包含 workspace。预留栈不是实测高水位，单函数栈不是完整调用链上界。ARM ELF 未在板上执行；原生消费者另验证了布局公式和真实初始化一致。

复现与产物说明见 [Boot 测量工具](tools/boot_footprint/README.md)。实际 MCU、Boot 分区、PHY/Flash 驱动、看门狗、断电恢复、ISR 栈和生产密码 provider 仍需要对应产品条件；本仓库不提供未经验证的板级保证。
