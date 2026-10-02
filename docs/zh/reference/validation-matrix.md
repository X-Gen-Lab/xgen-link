# 验证矩阵

## 协议覆盖

| 范围 | 依据 |
| --- | --- |
| wire v3、TLV、CRC、未对齐字节跨度 | `test/unit/wire/test_wire.cpp`、`test/unit/datalink/test_parser.cpp`、frame 性质测试 |
| 长度回绕、借用 SACK、失败视图清零及 raw/stream 一致 | `test/unit/wire/test_frame.cpp`、`test_wire_sack_view.cpp`、`test_wire_rx_contracts.cpp`、`test/unit/transport/test_transport_sack_view.cpp` |
| 固定路由及认证帧转发 | `test/unit/network/test_route.cpp`、`test/unit/network/test_network.cpp` |
| 精确 scope、原子 ACK、RX 保留、重传、RESET 与关闭 | `test/unit/transport/test_transport.cpp`、transport 性质测试 |
| RX/TX 无副作用拒绝、延迟快重传、终止错误与唯一存储归属 | `test_transport_rx_contracts.cpp`、`test_transport_tx_contracts.cpp`、`test_transport_runtime_contracts.cpp`、`test_transport_storage.cpp`（均位于 `test/unit/transport/`） |
| 分片预算、重叠、超时、消息大于窗口 | `test/unit/transport/test_fragment.cpp`、memory 性质测试 |
| 显式可信会话、nonce/AAD、防重放、关闭槽 | `test/unit/security/test_security.cpp` |
| 静态认证丢 ACK 恢复、应用只交付一次 | `test/unit/api/test_send.cpp` |
| 静态工作区、背压、回绕、存储复用 | `tools/static_workspace_smoke.c` |
| 一次后端预留、运行期不调用后端分配 | `test/unit/api/test_footprint.cpp`、memory 性质测试 |
| 公开安装包 C 消费者、ABI/profile 拒绝 | SDK 消费者 CTest 及 instance 测试 |
| 精确创建错误、纯预算期限查询与时间回绕 | `test/unit/api/test_api_contracts.cpp` |
| 认证配置预检、基本 ACK 容量及较小转发路由 | `test/unit/api/test_config_consistency.cpp` |
| 所有发送入口的消息上限、终止 scope 拒绝及布局无副作用 | `test/unit/api/test_send_policy.cpp` |
| ACK 长度溢出、初始化失败释放、PHY 错误与恢复 | `test/unit/wire/test_coverage_wire.cpp`、`test/unit/api/test_coverage_api.cpp`、`test/integration/test_coverage_delivery.cpp` |
| RTT 观察、workspace 预留量、流式 CRC 分类 | `test/unit/api/test_stats.cpp`、`test/unit/datalink/test_datalink.cpp` |
| Boot 关闭统计和详细诊断时保留错误契约 | `tools/static_workspace_smoke.c` 的条件分支 |
| Boot/Lite 可靠交付、容量拒绝、parser 和跨窗口分片 | `test/unit/api/test_bounded_profile.cpp` 的按能力测试 |
| Boot 逻辑 TX 32 字节扩展上限与 raw wire 独立边界 | `test/unit/network/test_boot_network.cpp` |
| 多配置发布/安装、导出能力与预算拒绝 | `test/cmake/test_profile_builds.py`、`test/cmake/test_footprint_budget.py` |
| 四组资源配置 API、固定 RTO 安装测试及标签完整性 | `test/cmake/test_transport_resource_config.py` |
| 公开/私有头文件独立 C11/C++17 编译、逐例发现、种子回放 | `test/cmake/HeaderContracts.cmake`、分类与 runner 契约测试 |

## 构建覆盖

最新 Boot 预算收敛阶段，GNU Full/Embedded、MSVC Full 各 817 项，Boot 222 项、Lite 314 项，Full 固定 RTO 协议 814 项。路由的 30 项常规测试覆盖所有 profile，新增自有数组重载、重叠复制、高频轮询和 ACK/SACK 边界。精确源码和质量结果见根目录 `design/validation/boot-budget-closure.md`。

此前 API 阶段本地 GNU Full/Embedded、MSVC Full 各通过 808 项，Boot 188 项、Lite 279 项；Full 自适应/固定 RTO 的协议集合分别为 801/805 项，四组资源配置执行指定 API 共 196 项，构建/安装契约 7 项。全部历史 510 名保留。各环境、源码身份及质量证据见根目录 `design/validation/api-refinement.md`；容量为 1 的 API 集合不代表全部私有 transport 测试。

本地 Full 矩阵包括协议回归及四个主机示例。Boot、Embedded Lite 和 Embedded 分别以关闭协议 libc fallback 的 C11 构建，执行静态工作区及安装包消费者测试；显式开启 GoogleTest 后，Boot/Lite 按能力测试，Embedded 执行完整历史回归。独立基础包拥有各自的行为、最小消费、安装和质量矩阵；协议负责五包集成。来源 core 的测试只记录为迁移基线，不代替新提供者验收。

此前五包迁移的中间检查点 `6927196` 通过 4/4 CompactWindow；有效 RED 见 `b169a10`。该阶段最终 Full 在 GCC/MSVC 各通过 8/8 CTest、510/510 GoogleTest，Embedded 4/4 CTest（510 项 GoogleTest）、当时默认五子模块 Boot 2/2 CTest 通过。memory 与 containers 各自通过 GCC/MSVC 及共享质量，core 活跃实现和旧 gitlink 已退出。这些历史结果的固定提交、覆盖率、安装入口负例和完整资源口径见仓库 `REFACTORING_STATUS.md`。

现行正式消费限定为预提供 targets 或安装包，源码开发使用独立 dev 装配。五个开发依赖与共享质量工具固定到已发布提交。前一独立交付阶段的远端全新克隆 GNU Full、Embedded 各执行 561 项（554 个 GoogleTest 加 7 项 smoke/示例），Boot 执行 2 项，各配置实际完成安装消费。这些结果属于前一阶段，不代替后续源码变更验证。

前一轮协议结构重构的本地 GNU Full/Embedded 和 MSVC Full 各执行 582 项（575 个 GoogleTest 加 7 项 smoke/示例），GNU Boot 执行 2 项；当时独立头检查覆盖 4 个公开头、24 个私有头和生成配置头的 C11/C++17 首包含。最新 sanitizer、覆盖率、ELF、源码提交与远端执行状态由根目录 `REFACTORING_STATUS.md` 记录。

本轮生产依赖契约扩展到 11 项，逐用例分类具有独立 CTest 策略回归，全部公开、私有及生成配置头分别以 C11/C++17 首包含编译。所有原有 510 个协议测试名称由版本控制清单逐一保留。更早阶段的通用组件测试已随实现迁往各自仓库，不与本轮协议清单混淆。主机生产覆盖率按行、函数、分支分别阻断，具体分子、分母、工具和源码提交见对应报告。

干净提交 `1b0ce7e` 的[远端 CI](https://github.com/X-Gen-Lab/xgen-link/actions/runs/36807629994)全部通过：Windows/MSVC、Linux、macOS 各 561 项，ASan/UBSan 560 项，CI Boot/Embedded 分别 2/3 项。生产覆盖率为行 3671/4010（91.5%）、函数 249/250（99.6%）、分支 2314/2865（80.8%）；Cppcheck、Clang-Tidy、严格文档、安装消费与发布验证均通过。八份 artifacts 的 GitHub SHA256 已核对，报告源码身份一致且干净。

## 资源与产品边界

当前默认普通/LTO Flash 为 **16,100/12,780 B**；small（窗口 1、桶 1、固定 RTO）为 **15,868/12,612 B**。MTU128/64 的 small 工作区为 1,040/848 B，含 1 KiB 预留栈 RAM 为 2,128/1,936 B；默认分别为 1,072/880 B 和 2,160/1,968 B。八项预算全部通过，默认 LTO 的原有超限已消除，门槛未放宽，RAM 与单函数栈不增加。以下旧阶段数字保留其历史口径，不代替当前报告。

Cortex-M0 探针记录真实链接的 ELF、map、目标 ABI 工作区公式及栈使用文件，可按 `tools/boot_footprint/README.md` 复现。原生运行另行比较布局公式并执行真实初始化。

wire 优化后的固定 GCC 15.2.1、MTU/RX 128 和 64 完整 ELF 均为 Flash 15,932 B，较上一轮 16,060 B 减少 128 B；启用显式 LTO 为 12,600 B。workspace 分别为 1,136/944 B，含预留 1,024 B 栈的 RAM 分别为 2,224/2,032 B，均未增加。最大单函数栈为普通模式 272 B、LTO 312 B。四组合无堆或未解析符号，通过各自独立 policy。普通 16 KiB 门槛和 LTO 12,800 B 门槛均为回归限额，8 KiB Flash 设计目标尚未达到。对照、取舍与复现见 [Wire 优化](wire-optimization.md)。

CI 矩阵包含 Boot/Lite 能力测试、Embedded 完整回归、多配置安装消费及 MTU128/64 × LTO OFF/ON 四个 ARM 资源门槛；本地脚本通过不等于该新矩阵已在远端执行，须以当前提交的实际结果为准。

上述 wire 数字是历史基线。transport 优化后的本地最终 ELF 为非 LTO/LTO Flash 16,116/12,792 B，MTU128/64 工作区 1,064/872 B，含 1 KiB 预留栈 RAM 2,152/1,960 B。普通 ACK 已知调用链从 step 起为 988/844 B，尚不含调用者、真实驱动或 ISR；预留栈不是产品峰值证明。TDD、构建矩阵、覆盖率、资源取舍及未执行项以仓库根 `design/validation/transport-optimization.md` 为准。

未验收真实板卡、Flash 断电、ISR 嵌套、完整调用链栈上界、DMA lease、生产密码 provider 或跨重启新鲜性。Boot 升级使用主机 Flash 模型，CRC16 只用于意外损坏校验。Linux sanitizer 的实际运行单独归档，不能从 Windows 结果推断。
