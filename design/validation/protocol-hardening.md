# 有界恢复与资源优化验证记录

日期：2026-10-01。起点：`af9799a8c611e7213759473f1758b3392bed6a3a`。实施分支：`feat/independent-memory-containers`。设计决策见 [ADR 0006](../adr/0006-bounded-recovery-runtime.md)。

## 已实施范围

- 重复 SACK 的成功快重传有界，BUSY 保留机会；首次可靠 RX 接纳失败归还无历史 peer；RTT 中间值防溢出；重传和 network TX 计数按成功提交更新。
- parser 超时策略归实例，期限独立于 PHY 轮询和 RX 字节预算；step 保留首个错误并继续处理已读字节和 transport 维护。
- 拒绝认证广播，拒绝不消耗 nonce。新增兼容的 scoped zero-copy 与纯布局查询；原请求 ABI、旧零 scope 行为保持。
- 增加精确资源类别报告，复用唯一布局计划；删除私有 link、packet 和层上下文的冗余状态，security 持有唯一认证策略。
- 合并 transport TX 私有头；Boot 逻辑发送扩展缓冲缩为 32 B，保留 wire 的完整扩展长度规则。
- 增加 Embedded Lite 304，原 profile 身份不变；Boot/Lite 按能力执行 GoogleTest，安装包验证必需能力，多配置消费传递 Debug/Release。
- CI 固定 ARM 工具链归档与校验和，分别对 MTU/RX 128、64 的完整 ELF 执行资源、禁堆与未解析符号门禁。

本轮没有修改五个基础包的实现，没有引入生产子模块或隐式下载。旧 wire v3 和已有公开结构 ABI 保留；新增查询和请求类型单独扩展。

## 真实 RED/GREEN

| 行为 | RED 检查点 | GREEN 检查点 |
| --- | --- | --- |
| 认证广播与普通/原地/转发成功计数 | `9d60679` | `53e07f0` |
| 重复 SACK、首次准入、RTT 边界及重试计数 | `1bfa19b` | `0c453e8` |
| parser 独立期限、回绕、零预算与首错传播 | `622eaba` | `d819652` |
| scoped 原地发送及无副作用布局查询 | `6791f1d` | `4b93997` |
| 精确工作区分类与失败输出保护 | `86b020d` | `4b93997` |
| 有界 profile、安装能力与多配置消费 | `2a37987`、`3a20b1b`、`34baffa` | `18b2364` |
| ARM 报告身份、限额与基线检查 | `bea136a` | `f804e31` |
| Boot 32 B 逻辑扩展与原 wire 解码能力 | `df9b7af` | `83f90a6` |

新 API 的 RED 包括真实消费者编译失败，并非预先存在的运行时断言。日志保留在 `out/opt-root`、`out/opt-transport`、`out/opt-wire`、`out/opt-build`。原 510 个历史名称保持，新增测试继续执行真实生产 C 实现。

完整矩阵另捕获升级示例回归：新 step 正确传播慢 Flash 接纳的 BUSY，旧示例却立即退出。失败日志保存在 `out/opt-linux/evidence/full-test-initial` 和 `out/opt-wire/example-red.log`。`bf7620b` 使示例接受设备端可恢复 BUSY 并继续调度，未吞掉其他错误。示例和相关聚焦回归 21/21 通过；最终输出为 8 块写入、1 次 BUSY、1 次丢 ACK、2 次重传和正确 CRC。

## 本地验证

| 验证 | 实际结果 | 证据 |
| --- | --- | --- |
| Windows GNU 13.2 Full / Embedded | 各 613/613 CTest；各 606 GoogleTest，7 项 smoke/示例；runner 各 6/6 | `out/opt-root/native-matrix.json`、`{full,embedded}-{test,runner}.log` |
| Windows GNU Boot / Embedded Lite | 66/66（64 GoogleTest）/ 101/101（98 GoogleTest），均含真实安装消费 | `out/opt-root/{boot,embedded-lite}-test.log` |
| MSVC 19.40 Full | 613/613；runner 6/6 | `out/opt-root/msvc-{build,test,contracts}.log` |
| GNU / MSVC 构建契约 | Boot、Lite、Debug/Release、安装能力四类均通过；MSVC Boot 最终 66/66 | `out/opt-build/contracts-green.log`、`msvc-boot-final.log` 及同目录契约报告 |
| Linux GNU Embedded Lite MinSizeRel | 101/101，含安装消费与 C/C++ 头契约 | `out/opt-linux/lite-build-test.log`、`lite-ctest.xml` |
| Linux GNU Full | 613/613，固定 shared test runner | `out/opt-linux/evidence/full-test` |
| Linux ASan / UBSan | 612/612，含泄漏检查与错误立即退出 | `out/opt-linux/evidence/asan-test` |
| Boot / Lite 固定共享测试入口 | 实际 test inventory、JUnit 与执行结果一致，分别 66/101 项通过 | `out/opt-root/evidence/{boot,embedded-lite}/quality-test.json` |
| 完整生产覆盖率 | 行 3794/4124（92.0%）、函数 281/282（99.6%）、分支 2407/2954（81.5%） | `out/opt-linux/evidence/full-coverage` |
| 头契约和主机 benchmark | 28 个公开/私有/生成头分别 C11/C++17 首包含；三个 benchmark 实际执行通过 | 各配置构建日志；`out/opt-root/benchmark-{throughput,latency,memory}.csv` |
| 生产依赖 / 开发准备 / 发现策略 / 资源门禁单测 | 11/11、11/11、3/3、5/5 | `out/opt-contracts/run-bogtzueo/result.json`、`out/opt-build` |
| Cppcheck 2.21 | 全部 45 个生产翻译单元通过 | `out/opt-root/cppcheck.log`、`out/reports/quality-cppcheck.json` |
| Clang-Tidy 19.1 | 45 个生产翻译单元通过；最终 frame 注释位置另做同配置增量检查 | `out/opt-linux/evidence/full-tidy`、`final-tidy-frame.log` |
| 严格 Doxygen / MkDocs | 公开头告警检查及双语站点构建通过 | `out/opt-root/{docs,mkdocs}.log`、`out/opt-docs-site` |
| text / format / pre-commit / 双语 QA | 固定 clang-format 19.1.5 和全仓检查通过 | `out/opt-root/pre-commit.log`、`out/reports` |

各 profile 单独配置和编译，不能用 Full 的测试数量代替 Boot/Lite 能力覆盖。ASan 的 612 项比 Full 少独立安装消费者，该项由普通 Full 实际安装和执行；未发现越界、未定义行为或泄漏报告。性质测试基础种子为 `5785420`，保留失败回放入口。

覆盖率在最终构建后清理该构建目录的旧 gcda，再由同一批测试生成；报告包含全部 45 个生产 C 文件和 4 个含可执行代码的私有头，未缩减生产分母。三个 benchmark 检查真实模拟交付、ACK 完成及资源统计，仅表示主机可执行，不当作 MCU 吞吐、链路时延或板级性能证明。

最终文档收尾后，对 Full、coverage、ASan 三份快照分别核对 179 个构建/源码/测试文件的 SHA-256，均无变化，记录为 `out/opt-root/source-content-verification.json`。证据中的历史 HEAD 与当时 dirty 状态保留，不改写为后续文档提交。

## Cortex-M0 最终 ELF

固定 Arm GNU 15.2.rel1 / GCC 15.2.1、Cortex-M0、`-Os`、无 LTO、Boot、单 peer、窗口 1。两个配置的 MTU 和 RX 缓冲均分别为 128、64。包含真实消费路径、启动和实际链接的 C 库；报告不是静态库对象大小相加。

| 字节 | 前一轮 MTU 128 | 本轮 MTU 128 | 同配置变化 | 本轮首次 MTU 64 |
| --- | ---: | ---: | ---: | ---: |
| Flash | 15,692 | 16,060 | +368 | 16,060 |
| workspace | 1,168 | 1,136 | -32 | 944 |
| 静态 RAM，已含 workspace | 1,232 | 1,200 | -32 | 1,008 |
| 额外预留栈 | 1,024 | 1,024 | 0 | 1,024 |
| RAM 合计 | 2,256 | 2,224 | -32 | 2,032 |
| 最大已链接单函数栈 | 392 | 272 | -120 | 272 |

证据为 `out/opt-arm-128`、`out/opt-arm-64` 的 ELF、map、布局、`.su` 和 JSON，实际命令见 `out/opt-root/arm-matrix.json`。两份报告均没有堆服务或未解析符号，正式 policy 已通过；差异报告在 `out/opt-build/budget-result-{128,64}.json`。64 是首次测量，不虚构旧 64 对比。报告保留测量时真实源码提交、dirty 状态和内容哈希。

新增 ARM Action 的安装脚本另在 WSL Ubuntu 22.04 / Python 3.12.9 原样执行，实际下载官方 Linux 工具链，校验固定 SHA-256 并解包。随后两组真实 Linux ELF 和 policy 均通过，资源数值与 Windows 完全一致，无堆或未解析符号；证据在 `out/opt-build/arm-linux/{install.log,build-budget.log,summary.json}` 和各 `mtu*/` 产物。此为本地 Linux 重放，不代替远端 Ubuntu 24.04 Actions。

Flash 增加 368 B 是本轮修复及调度逻辑的成本，不能归类为 Flash 下降。16 KiB CI 回归上限与 8 KiB Flash 设计目标分开：**两种配置仍未达到 8 KiB Flash**；仅 MTU 64 的 workspace 小于 1 KiB。

1 KiB 栈为预留量，272 B 为最大已链接单函数静态栈，不代表最大调用链、ISR 嵌套或板上高水位。Boot 不含认证；PHY/DMA 生命周期、Flash 擦写、断电恢复、BSP 和密码 provider 仍须按产品验证。

## 交付边界

本轮记录本地验证；新增远端 Actions 未执行，不借用之前提交的 CI 结果。未合并 main、创建 tag 或进行硬件验收。最新实施状态见 [REFACTORING_STATUS.md](../../REFACTORING_STATUS.md)，资源重放命令见[探针说明](../../tools/boot_footprint/README.md)。
