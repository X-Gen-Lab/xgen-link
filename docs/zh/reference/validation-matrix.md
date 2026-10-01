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

## 构建覆盖

本地 Full 矩阵包括协议回归及四个主机示例。Boot 和 Embedded 分别以关闭 libc 的 C11 构建，执行静态工作区及安装包消费者测试。独立基础包拥有各自的行为、最小消费、安装和质量矩阵；协议负责五包集成。来源 core 的测试只记录为迁移基线，不代替新提供者验收。

此前五包迁移的中间检查点 `6927196` 通过 4/4 CompactWindow；有效 RED 见 `b169a10`。该阶段最终 Full 在 GCC/MSVC 各通过 8/8 CTest、510/510 GoogleTest，Embedded 4/4 CTest（510 项 GoogleTest）、当时默认五子模块 Boot 2/2 CTest 通过。memory 与 containers 各自通过 GCC/MSVC 及共享质量，core 活跃实现和旧 gitlink 已退出。这些历史结果的固定提交、覆盖率、安装入口负例和完整资源口径见仓库 `REFACTORING_STATUS.md`。

本轮移除 link 的基础组件子模块和生产源码路径，正式消费限定为预提供 targets 或安装包，源码开发改用独立 dev 装配。生产契约 10/10、dev Full 8/8 CTest、Embedded 4/4、Boot 2/2 均通过；Full/Embedded 各执行 510 个 GoogleTest。显式 dev 源码的 ARM 重跑结果与前轮相同。这些是本轮本地证据；MSVC 和 Linux sanitizer 本轮未重跑，远端与硬件未执行，详细路径见实施记录。

通用组件测试随归属迁移，已删除的平台接口测试不再适用；协议用例数减少不代表历史测试集合原样保留。

## 资源与产品边界

Cortex-M0 探针记录真实链接的 ELF、map、目标 ABI 工作区公式及栈使用文件，可按 `tools/boot_footprint/README.md` 复现。原生运行另行比较布局公式并执行真实初始化。

未验收真实板卡、Flash 断电、ISR 嵌套、完整调用链栈上界、DMA lease、生产密码 provider 或跨重启新鲜性。Boot 升级使用主机 Flash 模型，CRC16 只用于意外损坏校验。Linux sanitizer CI 已配置，不能从 Windows 结果推断它已执行。
