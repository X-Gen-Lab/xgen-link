# API 契约完善验证

## 范围与检查点

基线为 `58ab3b9`，工作分支为 `feat/independent-memory-containers`。本轮保留已有未跟踪的 `.spec-workflow/`，不推送、合并或创建版本 tag。

| 阶段 | 检查点与实际证据 |
| --- | --- |
| 发送与配置 RED | `a4fd837`：发送 15 项中 10 项失败；配置 7 项中 6 项失败，CTest 退出 8。新增创建错误与预算查询 API 取得预期缺声明编译 RED，退出 1 |
| 资源定制 API RED | `39244f0`：Full/RTO OFF 的 RTT 历史断言失败；window 1 的 4 项中 3 项失败。仅缺文件或私有测试编译失败的记录不计入该运行 RED |
| 小路由兼容 RED | `7e01834`：全局 128 B、认证 tag 32 B、64/128 B 混合路由被错误的逐路由 ACK 门槛拒绝 |
| 主实现 GREEN | `56b4020`：API/配置/发送/清理 43/43；相关发送等回归 351/351；Full 默认协议 800/800、固定 RTO 协议 804/804；四组公开 API 各 49/49 |

生产代码采用 C11，GoogleTest 1.16.0 使用严格 C++17。CTest 保留实际名称和 `unit;xgl` 标签；性质测试默认回放种子为 `5785420`。公开配置及请求结构体布局没有变化，也没有新增实例状态。

本地证据位于 `out/api-refinement/`：`send/`、`config/`、`matrix/` 和根 `root-api-final.xml`。四组公开 API 为 Full AUTO/RTO ON、Full AUTO/RTO OFF、Full window 1/RTO OFF、Embedded window 1/RTO OFF；这是明确选择的 49 项 API 集合，不代替所有资源组合的整库验证。

## 资源门槛补充 RED

固定 GCC 15.2.1、Cortex-M0、Boot window 1、RTO OFF、RX chunk 32，MTU/RX 128 和 64 各自构建最终 ELF。`56b4020` 的普通模式为 Flash 16,212 B，两个普通 policy 通过；LTO 为 12,844 B，超过既有 12,800 B 门槛 44 B，两个 LTO policy 退出 1。门槛保持不变，继续减少新增检查的代码开销。

RED 从 `71bfb73` 的独立、未修改 detached worktree 重新链接并保留在 `out/api-refinement/arm-red-{128,64}-lto/`，两项 budget 仍退出 1，避免后续重建覆盖失败产物。该检查点与 `56b4020` 的生产源码相同。

优化后报告为 `out/api-refinement/arm-final-small-{128,64}-{off,on}/boot-footprint.json` 和 `budget-result.json`。共享既有精确 scope 查找，常规发送在一次 owner 准备中校验所有 DATA 的失败状态，再按可靠性决定是否保留 owner；没有扩大预算或削弱拒绝规则。

| MTU/RX | 优化后 Flash，无 LTO / LTO | 工作区 | 静态 RAM | 加预留栈 RAM |
| --- | ---: | ---: | ---: | ---: |
| 128 B | 16,116 / 12,788 B | 1,040 B | 1,104 B | 2,128 B |
| 64 B | 16,116 / 12,788 B | 848 B | 912 B | 1,936 B |

四个既有 small policy 全部通过。工作区和 RAM 相对这些 policy 的同配置基线均不增加；普通 Flash 增加 16 B，LTO 不增加，保留新增行为检查的实际成本。单函数栈普通/LTO 为 192/208 B，额外栈预留为 1,024 B。小 profile 指编译 window 1、RTO OFF 和 RX chunk 32，不与默认 window 128/RTO ON 的探针混比。没有达到 8 KiB Flash 目标；栈预留不是实测调用链上界，探针不代表板级 Boot 验收。

默认 window 128/RTO ON 另行检查发现普通模式新增资源 RED：`f2e3310` 为 16,420 B，超过既有 16,384 B 门槛 36 B；修改前相同默认配置为 16,336 B。`047d422` 记录该失败，MTU64 的失败 ELF、map 和报告另外保留在 `out/api-refinement/arm-default-red-64-off/`。

最终复验将轮询/parser 的计算和最小值归并提为小函数，两个公开查询分别遍历；旧入口无需保留新入口的预算策略分支。默认普通模式降至 **16,348 B**，两个 MTU 均通过原 16,384 B 门槛；默认 LTO 为 **12,964 B**，与本轮前相同，仍超出原 12,800 B 门槛 164 B。最终默认报告在 `out/api-refinement/arm-final-default-{128,64}-{off,on}/`。本轮八组结果为六组通过、两组既有 LTO 超限；没有放宽门槛，也没有将 small 的通过冒充默认通过。

安装包 fixed-RTO 的四项测试标签也取得有效 RED：发现结果只包含 `xgl`，缺少 `unit;transport;fixed-rto`。新断言和发现 JSON 在 `out/api-refinement/matrix/fixed-rto-labels-red/`，真实安装消费和编译已执行。

转义修复后，四项安装测试及完整 7 项资源配置契约 GREEN。最终 runtime 调整后重新执行 API 196/196、Full 默认 801/801、RTO OFF 805/805、7 项构建/安装契约，分别保留 before/after 输入哈希，汇总为 `out/api-refinement/matrix/final-runtime-split-summary.json`；原 510 个历史名称全部保留，无失败、跳过、重复或禁用。

## 跨 profile 和编译器

| 本地环境 | 实际结果 | 证据 |
| --- | --- | --- |
| GNU 13.2 Full | 808/808 CTest，801 个协议测试和 7 项 smoke/示例 | `full-split.xml` |
| MSVC 19.40 Full | 808/808 CTest | `msvc-split.xml` |
| GNU 13.2 Embedded，libc fallback OFF | 808/808 CTest | `embedded-split.xml` |
| GNU 13.2 Boot，libc fallback OFF | 188/188 CTest，183 个按能力协议测试和 5 项 smoke/示例 | `boot-split.xml` |
| GNU 13.2 Embedded Lite，libc fallback OFF | 279/279 CTest，273 个按能力协议测试和 6 项 smoke/示例 | `embedded-lite-split.xml` |
| Linux GNU 11.4 Full | 808/808 CTest | `linux/final-runtime-split/evidence/full-test/` |
| Linux ASan/UBSan | 807/807 CTest，启用泄漏检查 | `linux/final-runtime-split/evidence/asan-test/` |

以上证据均位于 `out/api-refinement/`。Sanitizer 配置显式关闭安装消费者 smoke，该项由普通 Full 单独执行；没有跳过协议用例。新配置一致性测试对认证辅助头按编译能力包含，保留 Boot/Lite 真实编译失败及修复日志；没有为通过 Full 而排除该公共测试源。

独立 C11 消费者只通过安装包 `find_package(xgl CONFIG REQUIRED COMPONENTS reliable)` 和 `xgl::xgl` 使用两个新增入口，完成创建、初始化、无事件查询、销毁和 ABI 拒绝，退出 0。源码、构建日志和运行退出码在 `out/api-refinement/sdk-api-{consumer,build,split-exit.txt}` 和 `sdk-api-split-*.log`；没有引用本仓私有头或 GoogleTest。

资源探针调用的是既有静态创建与 timeout 入口；新增按预算查询和错误返回创建在主机/安装消费者实际执行。需要在目标镜像使用这些新增入口时，应按其真实链接集合重新测量，不能从未链接的函数推断 Flash 成本。

## 覆盖率、静态分析与源码身份

最终 runtime 逻辑的 Linux GNU 11.4 Full 执行 808/808；行覆盖为 **4128/4401（93.797%）**，函数为 **310/310（100%）**，分支为 **2740/3266（83.894%）**，三项均超过既有 80% 门槛。正式运行前只清理本任务 Full 构建目录的旧 GCDA，避免混用前一版计数。日志和机器报告位于 `out/api-refinement/linux/final-runtime-split/`。

固定 Cppcheck 2.21.0 检查 43 个生产翻译单元通过。Clang-Tidy 对内部 `parser_timeout` 的两个毫秒参数报告可交换签名；保留 `tidy-red.log` 和 `quality-tidy-red.json`，按现行 `docs/standards.md` 的局部规则添加参数单位说明及仅包围该签名的 NOLINT，不关闭全仓诊断。最终 Clang-Tidy 19.1.0 的 43 个生产翻译单元通过。格式、文本、严格 Doxygen 和双语 MkDocs 构建均通过。

Windows、ARM、矩阵及 Linux Full/coverage 对应 `047d422` 加已冻结的 runtime 逻辑修改。随后仅增加三行诊断说明；`comment-delta.json` 验证删除这三行后文件 SHA-256 恰好恢复，其他编译输入均未变化。各阶段保留独立摘要，不将工作区验收写成干净提交或远端执行；矩阵最终清单摘要为 `48304e8b8ee8a8176fad0b8d56800781221d0c99418ffe4e3d9cf7bb889e2804`。

注释后的 Linux Full/ASan 增量构建、Clang-Tidy 和 ASan/UBSan 执行完成，最终阶段前后编译输入摘要一致。`linux/final-runtime-split/verification-summary.json` 汇总工具、发现清单、JUnit、覆盖率和两阶段来源；最终没有失败、跳过或重复，30 项新增 API 测试均实际执行。独立只读复审未发现本轮引入的行为缺陷。

## 验证边界

本页逐项记录本地结果。没有远端 CI、真实 PHY/DMA、密码 provider 或硬件运行结果。默认 LTO 的既有预算超限仍未解决，8 KiB Flash 目标仍未达到；本轮 API 完善不等于完整产品验收。
