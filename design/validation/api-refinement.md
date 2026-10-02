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
| 128 B | 16,188 / 12,796 B | 1,040 B | 1,104 B | 2,128 B |
| 64 B | 16,188 / 12,796 B | 848 B | 912 B | 1,936 B |

四个既有 small policy 全部通过。工作区和 RAM 相对这些 policy 的同配置基线均不增加；普通/LTO Flash 分别增加 88/8 B，保留新增行为检查的实际成本。单函数栈普通/LTO 为 192/208 B，额外栈预留为 1,024 B。小 profile 指编译 window 1、RTO OFF 和 RX chunk 32，不与默认 window 128/RTO ON 的探针混比。没有达到 8 KiB Flash 目标；栈预留不是实测调用链上界，探针不代表板级 Boot 验收。

默认 window 128/RTO ON 另行检查发现普通模式新增资源 RED：`f2e3310` 为 16,420 B，超过既有 16,384 B 门槛 36 B；修改前相同默认配置为 16,336 B。LTO 为 12,972 B，仍超过 12,800 B；修改前的 12,964 B 已超限。四个默认报告保存在 `out/api-refinement/arm-final-default-{128,64}-{off,on}/`，继续消除兼容查询入口引用公共实现时保留的额外运行期分支，不将 small 的通过冒充默认通过。

安装包 fixed-RTO 的四项测试标签也取得有效 RED：发现结果只包含 `xgl`，缺少 `unit;transport;fixed-rto`。新断言和发现 JSON 在 `out/api-refinement/matrix/fixed-rto-labels-red/`，真实安装消费和编译已执行。

转义修复后，四项安装测试及完整 7 项资源配置契约 GREEN。冻结阶段的 API 196/196、Full 默认 801/801、RTO OFF 805/805、7 项构建/安装契约分别保留 before/after 输入哈希，汇总为 `out/api-refinement/matrix/final-summary.json`；原 510 个历史名称全部保留，无跳过。

## 跨 profile 和编译器

| 本地环境 | 实际结果 | 证据 |
| --- | --- | --- |
| GNU 13.2 Full | 808/808 CTest，801 个协议测试和 7 项 smoke/示例 | `root-full-final.xml` |
| MSVC 19.40 Full | 808/808 CTest | `root-msvc-final.xml` |
| GNU 13.2 Embedded，libc fallback OFF | 808/808 CTest | `embedded-final.xml` |
| GNU 13.2 Boot，libc fallback OFF | 188/188 CTest，183 个按能力协议测试和 5 项 smoke/示例 | `boot-final.xml` |
| GNU 13.2 Embedded Lite，libc fallback OFF | 279/279 CTest，273 个按能力协议测试和 6 项 smoke/示例 | `embedded-lite-final.xml` |

以上 XML 均位于 `out/api-refinement/`。新配置一致性测试对认证辅助头按编译能力包含，保留 Boot/Lite 真实编译失败及修复日志；没有为通过 Full 而排除该公共测试源。

独立 C11 消费者只通过安装包 `find_package(xgl CONFIG REQUIRED COMPONENTS reliable)` 和 `xgl::xgl` 使用两个新增入口，完成创建、初始化、无事件查询、销毁和 ABI 拒绝，退出 0。源码、构建日志和运行退出码在 `out/api-refinement/sdk-api-{consumer,build,exit.txt}`；没有引用本仓私有头或 GoogleTest。

资源探针调用的是既有静态创建与 timeout 入口；新增按预算查询和错误返回创建在主机/安装消费者实际执行。需要在目标镜像使用这些新增入口时，应按其真实链接集合重新测量，不能从未链接的函数推断 Flash 成本。

## 验证边界

本页逐项记录本地结果。跨编译器、sanitizer、覆盖率、静态检查和最终资源复验完成后补充；没有远端 CI、真实 PHY/DMA、密码 provider 或硬件运行结果。
