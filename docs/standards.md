# 工程约定

本仓采用 X-Gen 工程规范 1.0.0 本地基线。权威正文由 xgen-roadmap 维护，初始已提交来源是 `c018272a1bfa0a0ad0e811ceedcb22862640ed98`。采用范围还含随后形成的 C-020、C-021、DOC-013 空行规则。规范来源与本仓补充执行如下。

## 语言与格式

- 自有生产代码使用 C11，主机 GoogleTest 和 GoogleMock 使用严格 C++17。生产构建默认只编译和安装库。测试与文档按需启用。
- 自有 C 和 C++ 源码使用锁定的 clang-format 14.0.6 和根 `.clang-format`。范围由 `.clang-format-dirs` 声明。CMake 使用锁定的 gersemi 和根 `.gersemirc`。检查当前版本及命令见[开发工作流](zh/development/workflow.md)。
- C、C++ 和 CMake 使用四空格和 80 列目标。C 和 C++ 使用同行花括号、靠类型的指针星号、缩进的 case 标签和顶格预处理指令。尾注前留两空格。在独立定义和声明组之间保留一空行，函数内的非空语句按职责分段。Doxygen 紧邻对应声明或定义。
- CMake 按职责分组参数，引用路径，保留有意的列表展开、生成器表达式及静态库链接顺序。目标依赖和用法要求显式标记 `PUBLIC`、`PRIVATE` 或 `INTERFACE`。私有头、实现定义和警告选项保持在所属目标。
- 文本使用 UTF-8、LF 和末尾换行。YAML 使用两空格，Makefile 的命令使用制表符，Markdown 可使用行尾空格表示换行。Windows 批处理文件遵循 `.gitattributes` 的 CRLF 配置。

## 命名与注释

- 公开函数沿用 `xgl_` 前缀及模块和动作命名。C 类型用小写下划线和 `_t` 后缀，宏、枚举值和具名常量用大写下划线及适用的模块前缀。文件作用域可变静态对象用 `s_`，确需跨文件共享的可变对象用 `g_`。公开 SDK、示例暴露的端口入口及 CTest 用例标识遵守各自的兼容合同。
- C 和 C++ 注释使用 `/* ... */`。采用反斜杠 Doxygen 标签。常规块内的标签正文从第 21 列开始，按所在代码的缩进计算。需要顶层分隔线时，C 和 C++ 使用 79 字符的块注释横线，CMake 使用 77 字符的井号横线。
- 文件开头的 Doxygen 块为头文件记录 `\file`、`\brief`、`\author`，为实现文件另记录 `\version`、`\date`、`\copyright`。`\version` 与 `cmake/XglVersion.cmake` 中的当前 SDK 版本一致。沿用有据可查的项目署名和引入日期，版权年份按原记录维护。
- 公共头写清调用上下文、输入和输出单位、所有权、借用截止、失败状态和下一步责任。`\param` 及 `\return` 等声明说明放在头文件。实现注释记录状态转换、执行顺序、容量与恢复约束。
- 协议定义、公开 API、工具输入、方案和执行证据各有权威源。文档按读者任务组织，并按[文档维护](zh/development/documentation.md)同步中英页面。

## 设计与评审

| 原则 | 在本仓的约束 |
| --- | --- |
| KISS | 用显式数据和直接控制流表示校验、状态转换与唯一所有权。工作区固定并有界。中间层需要明确职责，简化前核对容量、校验、错误及恢复。 |
| YAGNI | 按当前协议和实际场景选择能力。Boot 优先使用现有档位及资源选项。退役功能同步检查入口、配置、测试、打包与文档。 |
| DRY | 工作区尺寸、报告与初始化共用规划器，CMake 档位派生配置头和安装身份。合并前核对共同语义，保留原始输入与已验证视图、可靠留存与同步借用的边界及各自的故障验证。 |
| 单一职责 | 按状态所有权和变化原因划分模块。Wire 定义编码，parser 组帧，Network 管路由，Transport 管 scope 与可靠状态，Security 管认证状态，workspace 管资源，产品拥有调度和更新策略。 |
| 开闭原则 | 用现有 profile、配置和窄端口满足实例变化。增加线上或运行语义时配套更新接口、回归和权威规范。 |
| 替换原则 | allocator 满足对齐、尺寸和所有权合同，PHY 遵守整帧准入及同步借用，provider 遵守上下文、借用和成功失败合同。替换时核对能力、时钟单位和恢复行为。 |
| 接口隔离 | 公开窄接口仅依赖对应能力所需的类型和操作。可选能力按受支持的 profile 构建。 |
| 依赖倒置 | 协议依赖自身 PHY 与回调合同、显式时间和可选认证 provider。产品适配层承担驱动、OS、任务和恢复，实现借用与执行上下文边界。 |

抽象前核对真实使用场景及保留的行为差异。审查 CPU、Flash、RAM、栈及额外间接调用时，用相同工具链、优化、MTU、profile、LTO 和调用路径比较。资源结论应关联实际目标 ELF、map、workspace 和栈测量。产品另验证板级时延、Flash、DMA、ISR 和完整调用链。协议所有权和执行边界见[架构](zh/explanation/architecture.md)和[公开 API](zh/reference/api.md)。

## 验证与证据

新增或修复行为须保留在生产实现上取得的 RED/GREEN。等价搬迁保留既有测试和差分证据。记录 CTest 名称、标签和实际执行结果，性质测试保留可回放种子。将主机、交叉链接、sanitizer、远端 CI、发布和硬件验证分别对应到实际工具、参数和产物。执行入口见[开发工作流](zh/development/workflow.md)和[验证](zh/development/validation.md)。

使用静态分析时，读取当前完整的生产编译数据库，覆盖公开头和 `src` 自有实现。所有公开、私有及生成配置头应以 C11 和 C++17 单独首包含。新增局部诊断例外要写明规则、范围、必要性、补偿测试及复核条件。

`.clang-tidy` 提供编辑器和手动审查配置。CI 通过 `scripts/ci/quality_tools.py` 在 Full Advanced 和 Boot Small 的实际编译数据库上运行 clang-tidy 14 的 portable C 检查集，同时执行 Cppcheck 的 `warning`、`performance`、`portability` 检查。CI 把所选 clang-tidy 诊断提升为错误。CI 检查集以脚本为准，版本较新的 IDE 可继续提供其额外诊断。

## 局部诊断说明

| 检查 | 精确范围 | 依据与补偿验证 |
| --- | --- | --- |
| `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` | `.clang-tidy` 对可选 Annex K API 检查的排除 | 受支持的 C11 工具链使用通用字节例程；源码检查长度、容量与重叠，其他内存分析和边界测试继续覆盖。 |
| `bugprone-easily-swappable-parameters` | 源码中对应 `NOLINTBEGIN/END` 的单个函数签名 | 相同整数类型具有不同的时间、单位、容量或协议身份。对应调用、向量、会话及边界测试覆盖；参数或调用变化时复核。 |
| `bugprone-casting-through-void` | 已归属协议队列的 `XGL_LIST_ENTRY` 调用行 | 容器宏仅从属于该队列的有效节点恢复拥有对象。回归覆盖移除、重传释放和会话清理。 |
| `clang-analyzer-optin.performance.Padding` | `xgl_transport_peer_state_t` | 主机布局建议不足以决定 MCU 切割。通过实际配置的 workspace 与 ELF 验证，字段排列变化时重测。 |

诊断解释须匹配编译器、ABI 和系统头。缺少正确系统头或编译数据库时修复环境，再运行检查。
