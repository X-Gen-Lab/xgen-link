# 生产检查表

本页用于 MCU 或多节点系统上线前的工程审查。每个项目都应能在代码、配置、测试或运行日志中找到证据。

## 配置

- `source_id` 非 0，且不使用保留地址段。
- route table 覆盖所有目标节点，`max_frame_size` 与 PHY MTU 一致。
- `auth_required=true`，并提供有效 `auth_provider`。
- `auth_key_id` 与设备密钥管理系统一致。
- `enable_encryption` 和 `enable_compression` 保持关闭，直到 codec/security model 完整接入。
- production/noheap profile 中 allocator 行为明确，不隐式回退到 malloc。

## 安全

- auth provider 的 `tag_len` 固定且大于 0。
- `sign` 和 `verify` 对 header/extensions/payload 使用相同 AAD 边界。
- key id、nonce/material id 的生成和轮换由应用或安全模块管理。
- replay window 容量满足最大乱序窗口。
- 多跳 forwarding 场景已验证 TTL 修改后的 CRC/auth 处理。
- 错误认证、重放、错误 session 和错误 connection 的包不会 ACK。

## 可靠性

- reliable window 大小与链路 RTT、带宽和 RAM 预算匹配。
- retry limit 与应用容忍延迟匹配。
- ACK range/SACK 在 loss/reorder/duplicate 注入下通过。
- RESET/CLOSE 只清目标 peer/connection/session。
- 应用 callback 不阻塞协议主循环。

## 内存

- peer state、reliable queue、rx buffer、fragment reassembly 的上限可计算。
- fragment global budget 和 per-peer budget 均已配置。
- worst-case payload、fragment 数量和 route MTU 有容量分析。
- noheap smoke 通过。
- footprint report 符合目标 MCU RAM/Flash。

## 实时性和功耗

- ISR 只把 PHY RX 数据入队，不直接调用 parser/auth/transport。
- 主循环或 RTOS task 调用 `xgl_run()`。
- 使用 `xgl_next_deadline_ms()` 计算 sleep 时间。
- time provider 单调递增，并处理 wraparound。
- PHY send/receive 不在协议锁内长时间阻塞。

## 诊断

- 统计项能区分 CRC、auth、replay、route、MTU、timeout、fragment budget 错误。
- release build 保留必要的错误回调。
- 现场日志不输出密钥、tag 原文或敏感 payload。
- 长时 soak 覆盖多节点转发、重传和分片。

## 发布

- `ctest --preset gcc-test --output-on-failure` 通过。
- `xgl_release_validation` 通过。
- `xgl_docs` 通过。
- cppcheck 已安装并通过。
- SDK consumer smoke 通过。
- 工作区无未解释的源码改动。
