# XGL 文档

XGL 是面向资源受限 MCU 的生产级多节点可靠协议栈。它提供 v2 wire format、多 PHY 路由、可靠传输、ACK range/SACK、认证、分片重组预算和低功耗运行时 deadline。

## 当前能力

- 24-byte 固定 wire header，所有字段手动按 offset 编解码。
- 16-bit 节点地址和 32-bit packet number。
- TLV 扩展头支持 session、ACK range、SACK、fragment、security 和 route 元数据。
- 可靠传输以 `target_id + connection_id + session_epoch` 隔离 peer state。
- 生产配置要求认证 provider；zero-copy 路径遵守认证要求。
- `xgl_next_deadline_ms()` 支持 bare-metal/RTOS 低功耗调度。

## 能力边界

- 单帧 payload 长度仍由 `uint16_t payload_len` 限制。
- 大消息通过 FRAGMENT_EXT 分片重组。
- 压缩和加密是 reserved codec capability，当前生产路径拒绝直接启用。
- broadcast/multicast 地址段保留，当前可靠路径以单播为主。

## 推荐阅读路径

### SDK 使用者

1. [快速开始](getting-started/quick-start.md)
2. [配置](guide/configuration.md)
3. [Send API](guide/send-api.md)
4. [Zero-Copy](guide/zero-copy.md)
5. [低功耗运行时](guide/low-power-runtime.md)

### 协议维护者

1. [架构设计](protocol/architecture.md)
2. [实现映射](protocol/implementation-map.md)
3. [状态机](protocol/state-machines.md)
4. [Wire Format](protocol/wire-format.md)
5. [TLV 扩展](protocol/extensions.md)
6. [可靠传输](protocol/reliability.md)
7. [安全模型](protocol/security.md)
8. [验证矩阵](reference/validation-matrix.md)

### MCU 交付负责人

1. [移植](guide/porting.md)
2. [资源模型](guide/resource-model.md)
3. [生产检查表](guide/production-checklist.md)
4. [发布验证](reference/release-validation.md)
5. [静态分析](reference/static-analysis.md)
