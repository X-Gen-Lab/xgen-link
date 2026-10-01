# 工程规范采用与诊断例外

本仓采用 X-Gen 工程规范 **1.0.0 本地基线**，权威正文由 xgen-roadmap 的 `docs/standards/` 维护。初始已提交来源为 `c018272a1bfa0a0ad0e811ceedcb22862640ed98`；同时采用 2026-10-01 尚未发布的 C-020、C-021、DOC-013 空行增补。初始提交不包含全部后续增补，不能仅凭版本号推断两个工作区完全一致。

目前不声明已存在可获取的规范 tag，也不要求独立克隆具备某个兄弟目录。正式公开来源需要由 roadmap 发布固定提交或提供带来源和摘要的只读文档包。本文记录采用、模块补充和诊断边界，完整规则、变更与模板只由权威仓库维护。

## 适用范围与受控配置

- 自有生产源码、四个公开头、内部头、测试、示例、构建、开发工具和文档采用相应规则。第三方、显式准备的依赖、构建产物和生成文件按来源分别管理。
- 生产语言为 C11；GoogleTest/GoogleMock 主机测试使用严格 C++17。生成的公开配置头通过 C/C++ 编译和安装消费验证，生成文件由构建系统维护。
- `.clang-format` 与 2026-10-01 规范模板一致，SHA-256 为 `ffdb331b03ae4f6c5f75ee54d4afaa6d4741f5ac3ec57f55b0f04ad8ec396a2a`。formatter 检查可自动判定的布局，人工评审补充函数内语义分段和工具未支持的复杂声明。
- `.editorconfig` 将 C/C++ 文件模式合并，让 CMake/TXT 继承全局 4 空格；UTF-8、LF、缩进和行宽设置与模板对应，这是等效的本地组织。
- 工具版本和固定共享源码只在仓库 `tools/quality.json` 及其选定的 xgen-quality 包中维护；`tools/quality.py` 为薄入口。`.clang-tidy`、根 `Doxyfile` 与 `.pre-commit-config.yaml` 保存本仓配置，不复制共享检查实现。
- 精确生产/公共头范围由同一质量声明提供。不得把难测源码从生产范围删除以降低覆盖率分母，也不得用全局吞错绕过检查。

## 检查与证据归属

| 规范范围 | 本仓采用方式 |
| --- | --- |
| 编码、注释和空行 | 受控 formatter 与公共 C 头结构检查；接口所有权、时间、回调、ISR 和并发语义由评审确认 |
| TDD 与真实行为 | 新增/修复先 RED，再最小实现与 GREEN；协议 GoogleTest 链接真实生产 C 库 |
| 测试发现和执行 | 逐用例标记；保留 510 个历史名称；共享 runner 比较清单和 JUnit，拒绝空、禁用、跳过、重复或失败 |
| 随机输入 | 明确基础种子与每测试随机流，记录 XML 并提供失败回放命令 |
| 依赖与消费 | 产品 targets、安装包、开发装配分开；真实消费者验证版本、ABI、能力与冲突拒绝 |
| 静态分析 | 真实编译数据库驱动共享 Cppcheck、Clang-Tidy；工具不符、空输入和失败均阻断，具体诊断边界见下文 |
| 公开 API 文档 | 根严格 Doxyfile 检查四个公开头；站点继承同一规则，参数/缺文档告警失败 |
| 覆盖率 | 本配置自有生产源码的行、函数、分支分别至少 80%，保留未覆盖位置和分母 |
| 发布和 MCU | 固定组合、安装消费、profile 矩阵、最终 ELF 与资源预算；真实板级工作由产品验证 |

以上映射 GOV-001 至 GOV-005、C-020/C-021、DOC-013、TST-012/TST-014 及 QUAL-001 至 QUAL-013 的适用部分，描述要求和接入方式，不替代实际执行证据。源码授权、TDD 过程、接口语义、跨平台及硬件结论仍需评审与对应报告。

## 模块边界与迁移

协议维护 wire、datalink、network、transport、security 和公开装配 API；memory、containers、bytes、CRC、status 由独立组件提供。产品选择唯一组件版本、平台、资源及密码 provider，组件配置不联网获取依赖。同一实例由调用者串行化，存储与时间显式提供；PHY 返回前必须消费或复制字节。

本轮接入严格 C++17、逐用例发现、随机回放、共享质量入口及公共 API 文档。此前阶段的聚合测试与未执行检查保留在实施记录。可移植、无堆或主机通过不自动证明 ISR、密码 provider、DMA 生命周期、实时上界或断电恢复。

实际状态见仓库根目录 `REFACTORING_STATUS.md`，命令见[测试策略](zh/contributing/testing.md)、[静态分析](zh/reference/static-analysis.md)、[CI](zh/reference/github-ci.md)和[发布验证](zh/reference/release-validation.md)。本地、远端和硬件证据分别记录，规范来源公开化由 roadmap 完成。新增例外须登记规则、精确范围、理由、责任人、补偿验证和退出条件；待修复问题不作为永久通过。

生产源码使用 C11，测试使用 GoogleTest。共享入口对真实 CMake 编译数据库中的自有生产翻译单元执行 Cppcheck 和 clang-tidy；第三方及测试源码由各自任务验证。配置保持 clang-analyzer、bugprone 和 performance 检查，并将诊断视为错误。

私有头迁移后，`.clang-tidy` 的头文件过滤器同时包含公开 `include/xgl` 和自有 `src` 协议目录。该调整扩大同一套检查的可见范围；不把搬出公开 include 树当作跳过分析的理由。所有公开、私有及生成配置头均独立以 C11 和 C++17 首包含编译。

## 可移植 C 库配置

`.clang-tidy` 只排除 `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`。该检查建议将 `memcpy`、`memmove`、`memset` 等替换为可选 Annex K 的 `*_s` 接口，受支持的裸机 C11 环境和部分主机 C 库不提供这些接口。这不是缓冲区越界验证，也不意味着标准库调用自动安全。

其余内存、空指针、越界和生命周期分析仍启用。协议在调用前验证长度、容量和溢出，调用者承担公开接口声明的存储容量、生命周期及串行化前提；边界和失败行为由生产 target 的测试覆盖，最终镜像继续检查静态资源及禁堆约束。若支持的平台开始要求 Annex K，或任何字节复制的容量来源、重叠要求发生变化，应重新评估对应接口和本项配置。

## 局部诊断说明

| 检查 | 精确范围 | 依据与补偿验证 |
| --- | --- | --- |
| `bugprone-easily-swappable-parameters` | 源码中 `NOLINTBEGIN/END` 包围的单个函数签名 | 时间值、字节数、节点/连接/会话标识及 wire 字段具有相同 C 整数类型。保留现有调用顺序和明确参数名；签名之外不抑制该检查。API 契约、wire 向量、ACK/重传、会话隔离与边界测试验证字段语义；变更参数或引入新调用点时重新审阅。 |
| `bugprone-casting-through-void` | 已归属协议队列的 `XGCT_LIST_ENTRY` 调用行 | 共享容器宏通过字节偏移恢复实际嵌入节点的拥有对象，内部 `void*` 转换不改变对象。调用必须来自对应类型的有效队列遍历；该例外不能用于任意地址、错误成员或已释放对象。队列移除、重传释放和分片清理测试验证生命周期。 |
| `clang-analyzer-optin.performance.Padding` | `xgl_transport_peer_state_t` | 保留已测量的 profile 布局，64 位主机给出的最佳字段顺序不直接决定 MCU 布局。完整镜像和工作区测量继续约束资源；任何未来字段重排须独立验证所有 profile 的尺寸和布局。其他结构体仍执行 padding 检查。 |
| `clang-analyzer-core.DivideZero` | `xgl_workspace_prepare` 检查存储对齐的单个取模表达式 | `make_plan` 仅在 `xgm_size_class_measure` 成功后返回；提供者契约保证输出 `_Alignof(xgm_max_align_t)`，恒大于零。分析器未跨编译单元推导这个输出值。静态工作区的有效、未对齐、容量不足和 ABI 拒绝测试继续执行；更换 measure 提供者或契约时删除并重新评估该例外。 |

`fragment_free_reassembly_buffer` 是私有清理函数：非空 buffer 必须对应有效的拥有者 manager。其调用来自先验证 manager 的同步维护遍历；实现不再在计数语句中保留一个与后续释放前提不一致的冗余空检查。没有将非法内部调用伪装成公共支持行为，也没有新增空指针分析豁免。

## 分析环境

clang-tidy 必须使用与编译数据库一致的目标和系统头。Windows 中 MinGW GCC 的隐式 CRT 路径不能直接交给默认 MSVC 目标的工具解释；缺少 `string.h` 属于环境错误，不能通过跳过文件获得绿色结果。

本轮 Linux 本地分析使用真实 WSL GNU 11.4 构建、CMake 3.31.6、Python 3.12.9 和固定 clang-tidy 19.1.0；Cppcheck 2.21.0 使用 Windows GCC 编译数据库。二者均通过 `tools/quality.py` 执行，实际命令、工具版本、源码状态及退出结果保存在 `out/reports/quality-tidy.json` 和 `out/reports/quality-cppcheck.json`。本地通过与远端 CI、干净检出及硬件验收分别记录。
