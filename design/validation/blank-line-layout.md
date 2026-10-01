# 空行规范验收

日期：2026-10-01。本次在已有协议与基础组件迁移工作区上实施空行规范，不改变 C/C++ 代码行为。此前的功能重构与文档改动保持原状；本文件只记录本次新增验证，不替代 [功能迁移证据](independent-components.md)。

## 规则与工具

权威规范位于 xgen-roadmap 的 `docs/standards/coding-c-cpp.md`（C-020、C-021）及 `docs/standards/comments.md`（DOC-013）。规范 1.0.0 和工具 0.1.0 仍未公开发布，本次通过实际源码提交固定工具。

六个源码仓库和质量工具的受控 `.clang-format` 完全一致，SHA-256 为 `ffdb331b03ae4f6c5f75ee54d4afaa6d4741f5ac3ec57f55b0f04ad8ec396a2a`。固定 clang-format 19.1.5；配置限制连续空行、去除文件/块首与文件末多余空行、分隔独立定义，并明确使用 LF。

公共头中独立 API 文档组之间留一行，Doxygen 紧邻声明。共享工具的有限词法检查补充 formatter 未覆盖的常见 C 头结构；不将字符串、宏续行、字段尾注释或函数体语义阶段当作独立 API 组。条件包装、宏生成接口、一般 C++ 语法及语义分段仍需人工评审。自动门禁仅检查各仓 `public_headers` 明确声明的文件。

实际安装的质量工具从 Git 提交 `ed428bb0b0f4cdc5453490abcbd10b4121569442` 用 `git archive` 导出干净源码，在已有固定构建环境中离线构建 wheel，再用 `--no-index --no-deps --force-reinstall` 显式安装。安装未消费工具工作区原有的未提交改动。

本次 wheel SHA-256 为 `349962ae8dee9c1dc005a5b4879391d3f6f67654d969aef6ce2aecc847aea7ba`。这是本次构建产物的摘要，不宣称不同环境或时间重新打包会得到相同摘要。六仓的 `tools/quality.json` 均固定上述工具提交。

## 本地实际结果

| 仓库 | 自有 C/C++ 文件 | 本次空行变化文件 | format | pre-commit |
| --- | ---: | ---: | --- | --- |
| xgen-link | 148 | 13 | 通过 | 通过，仅格式钩子 |
| xgen-memory | 18 | 13 | 通过 | 通过，文本与格式 |
| xgen-containers | 14 | 12 | 通过 | 通过，文本与格式 |
| xgen-bytes | 9 | 0 | 通过 | 通过，文本与格式 |
| xgen-crc | 10 | 0 | 通过 | 通过，文本与格式 |
| xgen-status | 10 | 1 | 通过 | 通过，文本与格式 |

共检查 209 个源码文件。与本次开始前保存的快照相比，所有非空行均逐字一致，包括代码、注释及字符串；39 个文件仅增删空白行。变化计数先将原有 CRLF 规范为 LF，不将纯换行符转换计入空行变化文件数。没有修改第三方、构建产物或旧 core 历史源码。

源码分组由 formatter 和人工整理完成；格式入口只读，通过后不会自动暂存源文件。全量 `format` 也覆盖本仓自有未跟踪源码，避免只用 pre-commit 的已跟踪文件选择来冒充全量验证。公共头结构诊断为 0。另人工补齐 `xgl_protocol_io.h` 与 `xgl_security.h` 的已发现内部头分隔，不扩大公共头门禁范围。

质量工具新行为具有真实 RED/GREEN：RED 提交 `7bee4fb`，检查器 GREEN 提交 `94723f54fac4e36f69051612e7be560776ad0fc2`；最终提交仅进一步统一模板标题、验证版本和来源说明。82/82 Python 测试通过，包含 15 项空行测试及隔离 wheel 消费；全包行覆盖 510/521（97.89%）、分支覆盖 251/260（96.54%）。没有为纯源码排版新增生产行为测试。

规范中的 C/C++ 示例、模板解析及文档链接已检查；link 文档 QA、各仓差异空白检查通过。执行记录在本地 `build/blank-line-migration/`，包括初始快照、非空行比对、固定 wheel 来源及逐仓 format/pre-commit 报告。工具测试详细报告在 xgen-quality 的 `out/reports/blank-line-*`。

## 集成状态与边界

五组件的本地格式提交和 link 子模块指向见 [实施记录](../../REFACTORING_STATUS.md)。五个子模块工作区干净，其配置与工具固定提交已逐一核对。link 与 roadmap 的原有未提交工作仍保留；本次没有合并或推送整套功能重构。

link 新增薄 `tools/quality.py`、本地格式钩子和 CI 格式任务，本地、钩子及 CI 配置均调用同一个 `format`。启用自动提交钩子需要在已经安装固定工具的环境运行 `python -m pre_commit install`；本次已运行 `pre_commit run --all-files`，不将配置文件存在等同于所有开发环境已安装钩子。

远端 CI 需要可获取的固定质量工具提交和 `XGEN_QUALITY_REPOSITORY` 仓库变量。本次未运行远端 CI、未发布组件或工具。源码非空行不变且格式门禁通过后，没有重复此前的协议功能矩阵、覆盖率或 ARM 资源测试；本文件不将此前结果重记为本次执行结果。
