# 验证矩阵

## 协议覆盖

| 范围 | 依据 |
| --- | --- |
| wire v3、TLV、CRC、未对齐字节跨度 | `test/test_wire.cpp`、`test/test_parser.cpp`、frame 性质测试 |
| 固定路由及认证帧转发 | `test/test_route.cpp`、`test/test_network.cpp` |
| 精确 scope、原子 ACK、RX 保留、重传、RESET 与关闭 | `test/test_transport.cpp`、transport 性质测试 |
| 分片预算、重叠、超时、消息大于窗口 | `test/test_fragment.cpp`、memory 性质测试 |
| 显式可信会话、nonce/AAD、防重放、关闭槽 | `test/test_security.cpp` |
| 静态认证丢 ACK 恢复、应用只交付一次 | `test/test_send.cpp` |
| 静态工作区、背压、回绕、存储复用 | `tools/static_workspace_smoke.c` |
| 一次后端预留、运行期不调用后端分配 | `test/test_footprint.cpp`、memory 性质测试 |
| 公开安装包 C 消费者、ABI/profile 拒绝 | SDK 消费者 CTest 及 instance 测试 |
| ACK 长度溢出、初始化失败释放、PHY 错误与恢复 | `test/test_coverage_wire.cpp`、`test/test_coverage_api.cpp`、`test/test_coverage_delivery.cpp` |
| 头文件独立 C++17 编译、逐例发现、种子回放 | `test/cmake/HeaderContracts.cmake`、分类与 runner 契约测试 |

## 构建覆盖

本地 Full 矩阵包括协议回归及四个主机示例。Boot 和 Embedded 分别以关闭 libc 的 C11 构建，执行静态工作区及安装包消费者测试。独立基础包拥有各自的行为、最小消费、安装和质量矩阵；协议负责五包集成。来源 core 的测试只记录为迁移基线，不代替新提供者验收。

此前五包迁移的中间检查点 `6927196` 通过 4/4 CompactWindow；有效 RED 见 `b169a10`。该阶段最终 Full 在 GCC/MSVC 各通过 8/8 CTest、510/510 GoogleTest，Embedded 4/4 CTest（510 项 GoogleTest）、当时默认五子模块 Boot 2/2 CTest 通过。memory 与 containers 各自通过 GCC/MSVC 及共享质量，core 活跃实现和旧 gitlink 已退出。这些历史结果的固定提交、覆盖率、安装入口负例和完整资源口径见仓库 `REFACTORING_STATUS.md`。

现行正式消费限定为预提供 targets 或安装包，源码开发使用独立 dev 装配。完整协议实现已提交并推送功能分支，五个开发依赖与共享质量工具固定到已发布提交。远端全新克隆的 GNU Full、Embedded 各执行 561 项（554 个 GoogleTest 加 7 项 smoke/示例），Boot 执行 2 项，各配置实际完成安装消费；最新提交的验收与远端矩阵由根目录 `REFACTORING_STATUS.md` 记录。

本轮生产依赖契约扩展到 11 项，逐用例分类具有独立 CTest 策略回归，21 个源码头文件分别首包含编译。所有原有 510 个协议测试名称由版本控制清单逐一保留。更早阶段的通用组件测试已随实现迁往各自仓库，不与本轮协议清单混淆。主机生产覆盖率按行、函数、分支分别阻断，具体分子、分母、工具和源码提交见对应报告。

干净提交 `1b0ce7e` 的[远端 CI](https://github.com/X-Gen-Lab/xgen-link/actions/runs/36807629994)全部通过：Windows/MSVC、Linux、macOS 各 561 项，ASan/UBSan 560 项，CI Boot/Embedded 分别 2/3 项。生产覆盖率为行 3671/4010（91.5%）、函数 249/250（99.6%）、分支 2314/2865（80.8%）；Cppcheck、Clang-Tidy、严格文档、安装消费与发布验证均通过。八份 artifacts 的 GitHub SHA256 已核对，报告源码身份一致且干净。

## 资源与产品边界

Cortex-M0 探针记录真实链接的 ELF、map、目标 ABI 工作区公式及栈使用文件，可按 `tools/boot_footprint/README.md` 复现。原生运行另行比较布局公式并执行真实初始化。

未验收真实板卡、Flash 断电、ISR 嵌套、完整调用链栈上界、DMA lease、生产密码 provider 或跨重启新鲜性。Boot 升级使用主机 Flash 模型，CRC16 只用于意外损坏校验。Linux sanitizer 的实际运行单独归档，不能从 Windows 结果推断。
