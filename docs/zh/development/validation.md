---
id: development/validation
type: how-to
audience: maintainer
source_language: zh_CN
---

# 验证

## 协议覆盖

| 范围 | 依据 |
| --- | --- |
| Wire v1、TLV、CRC、未对齐字节跨度 | `test/unit/wire/test_wire.cpp`、`test/unit/datalink/test_parser.cpp`、frame 性质测试 |
| 长度回绕、借用 SACK、失败视图清零及 raw/stream 一致 | `test/unit/wire/test_frame.cpp`、`test_wire_sack_view.cpp`、`test_wire_rx_contracts.cpp`、`test/unit/transport/test_transport_sack_view.cpp` |
| 固定路由及认证帧转发 | `test/unit/network/test_route.cpp`、`test/unit/network/test_network.cpp` |
| 本地反馈出口、反向 MTU 与 PHY 背压 | `test/integration/test_network_multihop.cpp` |
| 精确 scope、原子 ACK、RX 保留、重传、RESET 与关闭 | `test/unit/transport/test_transport.cpp`、transport 性质测试 |
| RX/TX 无副作用拒绝、延迟快重传、终止错误与唯一存储归属 | `test_transport_rx_contracts.cpp`、`test_transport_tx_contracts.cpp`、`test_transport_runtime_contracts.cpp`、`test_transport_storage.cpp`（均位于 `test/unit/transport/`） |
| 显式可信会话、nonce/AAD、防重放、关闭槽 | `test/unit/security/test_security.cpp` |
| 静态认证丢 ACK 恢复、应用只交付一次 | `test/unit/api/test_send.cpp` |
| 静态工作区、背压、回绕、存储复用 | `test/smoke/static_workspace_smoke.c` |
| 一次后端预留、运行期不调用后端分配 | `test/unit/api/test_footprint.cpp`、memory 性质测试 |
| 公开安装包 C 消费者、ABI/profile 拒绝 | SDK 消费者 CTest 及 instance 测试 |
| 精确创建错误、纯预算期限查询与时间回绕 | `test/unit/api/test_api_contracts.cpp` |
| 认证配置预检、基本 ACK 容量及较小转发路由 | `test/unit/api/test_config_consistency.cpp` |
| 所有发送入口的单帧大小上限、终止 scope 拒绝及布局无副作用 | `test/unit/api/test_send_policy.cpp` |
| ACK 长度溢出、初始化失败释放、PHY 错误与恢复 | `test/unit/wire/test_coverage_wire.cpp`、`test/unit/api/test_coverage_api.cpp`、`test/integration/test_coverage_delivery.cpp` |
| RTT 观察、workspace 预留量、流式 CRC 分类 | `test/unit/api/test_stats.cpp`、`test/unit/datalink/test_datalink.cpp` |
| Boot 关闭统计和详细诊断时保留错误契约 | `test/smoke/static_workspace_smoke.c` 的条件分支 |
| Boot/Lite 可靠交付、容量拒绝与 parser | `test/unit/api/test_bounded_profile.cpp` 的按能力测试 |
| Boot 生成的 TX 扩展与 raw wire 的完整帧边界 | `test/unit/network/test_boot_network.cpp` |
| 多配置发布与安装、导出能力 | `test/contracts/build/test_profile_builds.py` |
| 四组资源配置 API、固定 RTO 安装测试及标签完整性 | `test/contracts/build/test_transport_resource_config.py` |
| 公开/私有头文件独立 C11/C++17 编译、逐例发现、种子回放 | `test/cmake/HeaderContracts.cmake`、分类与 runner 契约测试 |

## 构建和安装

在只准备 C11 工具链的干净构建目录验证默认生产库、Boot 生产库和源码子目录。独立安装消费实际编译、链接并运行公开 C11 和 C++17 程序，覆盖 ABI、profile、必需能力、资源身份和前缀冲突。正常主机测试按 [开发工作流](workflow.md) 执行。缺少本次测试清单、运行报告或失败退出码时，保留真实状态。

`ci` preset 选择 Full 档位，开启高级运行时和 GCC 覆盖率插桩。`run_ctest.py` 保存用例清单和 JUnit 报告，并拒绝跳过的测试。多配置构建逐配置保存结果。

```sh
cmake --preset ci
cmake --build --preset ci
python test/cmake/run_ctest.py --build-dir build/ci --config Debug
```

本地使用 CMake 时，运行 `cmake --build --preset ci --target xgl_release_validation` 构建并执行已注册的 CTest 用例，包含 SDK 搬移消费和无堆检查。如启用了本机构建的 `xgl_footprint` 目标，可用它报告静态库文件大小。

使用 `boot-small-host-test` 的配置、构建和测试 preset 验证单窗口槽与固定 RTO。`asan` preset 会让 AddressSanitizer 和 UndefinedBehaviorSanitizer 的诊断使测试失败。SDK 冒烟测试也会将两个独立消费者链接到经过插桩的库并运行。记录实际执行的 profile、编译器和 sanitizer。GoogleTest 依赖见 `test/CMakeLists.txt`。

同时保留每个配置的 `compile_commands.json`。静态分析使用当前生产翻译单元、生成头与编译标志。Windows、macOS、GCC 覆盖率、Native Release 和 TSan 的执行范围按 CI 对应任务核对。资源变更使用真实 ARM 编译链接及目标 map 复核，见下节。

## 资源和产品验收

用产品目标工具链、最终链接 map 和实际应用配置测量固件。比较时保持档位、帧与接收缓冲大小和优化选项一致。记录 Flash、workspace、静态 RAM 和栈空间，再在板上核对驱动缓冲、ISR 嵌套及栈峰值。

真实板卡、Flash 断电、DMA 借用、生产认证和重启后 nonce 新鲜性由产品验收。模拟与软件检查、ARM 链接、远端 CI、发布和硬件执行各自关联实际提交与产物。

## 历史证据

开发分支的[实施记录](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/REFACTORING_STATUS.md)、[重构计划](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/REFACTORING_PLAN.md)、[架构决策](https://github.com/X-Gen-Lab/xgen-link/tree/f09efca25abe84c344ce8c41d5645006c7b61df9/design/adr)和[验证证据](https://github.com/X-Gen-Lab/xgen-link/tree/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation)保存在固定提交。阅读历史数据时同时核对报告中实际受测的源码身份和构建配置。
