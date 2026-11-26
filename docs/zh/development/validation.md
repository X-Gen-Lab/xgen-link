# 验证

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

## 构建和安装

在只准备 C11 工具链的干净构建目录验证默认生产库、Boot 生产库、源码子目录和独立安装消费者。消费检查需实际编译、链接并运行公共 C 程序，验证 ABI、profile 和必需能力的错误组合会被拒绝。正常主机测试按 [开发工作流](workflow.md) 执行。缺少本次测试清单、运行报告或失败退出码时，保留真实状态。

准备发布所需的测试、分析与文档工具，配置本仓根目录的 `ci` preset，再运行 `cmake --build build/ci --target xgl_release_validation`。质量门禁要求行、函数和分支覆盖率各至少 80%。同时核对适用的 profile、编译器及 sanitizer 执行范围。质量工具和主机框架的版本来源见 `tools/quality.json` 与 `test/CMakeLists.txt`。

## 资源和产品验收

`tools/boot_footprint/README.md` 描述 Cortex-M0 探针复现方式。比较同工具链、profile、资源选项、MTU、RX 和 LTO 组合的目标 ELF。记录 Flash、workspace、静态 RAM、预留栈与最大单函数栈；板级完整调用链、驱动、ISR 叠加和实测栈峰值另外验证。

真实板卡、Flash 断电、DMA 借用、生产认证和重启后 nonce 新鲜性由产品验收。模拟与软件检查、ARM 链接、远端 CI、发布和硬件执行各自关联实际提交与产物。

## 历史证据

SDK 3 分支的[实施记录](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/REFACTORING_STATUS.md)、[重构计划](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/REFACTORING_PLAN.md)、[架构决策](https://github.com/X-Gen-Lab/xgen-link/tree/f09efca25abe84c344ce8c41d5645006c7b61df9/design/adr)和[验证证据](https://github.com/X-Gen-Lab/xgen-link/tree/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation)保存在固定提交。阅读历史数据时同时核对报告中实际受测的源码身份和构建配置。
