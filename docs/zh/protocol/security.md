# 安全模型

生产配置默认要求认证。测试和调试可以显式关闭，但 release profile 应启用 `auth_required`。

`auth_required=false` 表示允许未认证帧，并不表示忽略已认证帧。任何带 `AUTHENTICATED`/`SECURITY_EXT` 的帧都必须验签成功后才能交付。

## Auth Provider

`xgl_auth_provider_t` 提供：

- `sign`
- `verify`
- `tag_len`，最大为 `XGL_AUTH_TAG_MAX_LEN`
- `user_data`

当 `auth_required=true` 且 provider 缺失、`tag_len == 0` 或 `tag_len > XGL_AUTH_TAG_MAX_LEN` 时，初始化必须失败。
authenticated 配置还必须提供带 `malloc` 和 `free` 的 `memory.allocator`；
缺少该 allocator contract 时，`xgl_config_validate()` 会拒绝 production auth。

## AAD 和 Payload

基础头和扩展头经过认证模式的规范化规则后作为 AAD。payload 被认证但不加密。CRC 用于快速误码检测，认证 tag 用于防伪造、防篡改和防重放。

## 认证模式

XGL 明确区分两种安全模型：

| 模式 | 验证者 | 转发行为 | 认证域 |
| --- | --- | --- | --- |
| Hop-by-hop auth | 每一跳 | 每个转发节点验证并重签 | 可以包含当前 wire header，包括 `ttl` 和 `header_crc16` |
| End-to-end auth | 最终目的节点，在 datalink 校验后 | 转发节点不得重签 | 必须排除逐跳可变字段和链路校验字段 |

当前 XGL 的 `AUTHENTICATED`/`SECURITY_EXT` 路径采用 **End-to-end auth**：源端签名，目的端验证，中间节点只更新转发元数据和 CRC。

### 端到端 Canonical AAD

端到端认证签名的是 wire AAD 的规范化视图：

- `ttl` 按 0 参与计算，因为每转发一跳都会递减。
- `header_crc16` 按 0 参与计算，因为逐跳修改 header 后必须重算。
- `frame_crc16` 不参与认证输入，因为它位于认证 trailer 之后。
- 稳定的基础头字段、稳定的 TLV 扩展和 payload 仍然被认证保护。

| 字段/材料 | Auth input 处理 | 原因 | 证据 |
| --- | --- | --- | --- |
| 基础头 bytes | 作为 AAD 纳入 | 绑定稳定的 routing/session/packet 身份 | `src/wire/xgl_wire.c` |
| `ttl` | 以 0 纳入 | 转发每跳递减 TTL | `src/wire/xgl_wire.c`, `src/network/xgl_network_receive.c` |
| `header_crc16` | 以 0 纳入 | TTL 变化后 header CRC 会重算 | `src/wire/xgl_wire.c` |
| TLV extensions | 作为 AAD 纳入 | 绑定 session、security、fragment、route 和 data-type 元数据 | `src/wire/xgl_wire.c`, `src/wire/xgl_wire_ext.c` |
| Payload | 作为 payload input 纳入 | 保护应用/fragment 数据 | `src/wire/xgl_wire.c` |
| Authentication tag | 不纳入自身输入 | tag 由 provider 生成 | `src/wire/xgl_wire.c` |
| Frame CRC16 | 不纳入 | 它序列化在 auth trailer 之后 | `src/wire/xgl_wire.c`, `src/wire/xgl_frame_auth.c` |

未来如果新增每跳都会变化的扩展字段，必须使用同样的 canonical 规则排除，或者引入独立的 hop-by-hop 认证机制。不要把逐跳可变字段静默加入端到端 AAD。

## 决策：端到端认证

当前协议为 `AUTHENTICATED`/`SECURITY_EXT` 路径选择端到端认证。

原因：

- 中间节点无需持有源端签名密钥即可转发。
- 认证 tag 可以跨多跳保护 payload 和稳定的路由/session 身份。
- TTL 和 CRC 保持为 datalink/network 维护字段，而不是应用安全字段。

后果：

- 转发修改 TTL 后必须重算 header CRC 和 frame CRC。
- 转发必须保留原始认证 tag。
- 如果部署需要每条链路认证“上一跳”，应增加独立的 hop-by-hop tag，而不是复用端到端 tag。

## 认证 Trailer

认证 trailer 位于 payload 之后、frame CRC 之前。`SECURITY_EXT` 记录 `key_id`、`nonce_id` 和 `tag_len`。实际 tag 长度由 provider 固定声明，避免签名路径先试算再重签。

## 验证顺序

1. 检查 magic、version、header_len 和 payload_len。
2. 验证 header CRC。
3. 解析 `SECURITY_EXT`。
4. 当实例要求认证，或帧声明自己已认证时，验证 auth trailer。
5. 对已通过认证的帧检查 replay window。
6. 进入 network/transport 语义处理。

任何一步失败都不得交付 payload。

## Replay Window

反重放 key：

```text
source_id + connection_id + session_epoch + packet_number
```

Replay 检查使用三态结果：

- 新的认证包进入 network/transport 语义处理。
- ACK-eliciting 的 reliable 重复包允许进入 transport，用于在 ACK 丢失后重新生成 ACK/SACK；transport 的重复检测不得再次交付 payload。
- 非 reliable 重复包、旧 session 包、错误 connection 包，以及早于 replay window 的包，在进入 network/transport 前丢弃。

## 多跳转发

TTL 是每跳可变字段。转发修改 TTL 后，当前实现只重算 `header_crc16` 和 `frame_crc16`，并保留原始认证 tag。验签仍然有效，因为调用 provider 前会把 TTL 和 header CRC 规范化为 0。

## Reserved

加密能力当前保留。不要把 `enable_encryption` 当作可用生产加密路径。

## 密钥边界

XGL 不持久化密钥，也不规定密钥派生方案。生产应用应在 auth provider 内部完成密钥存储、轮换、key id 映射和硬件安全模块接入。

## 追溯

| 规则 | 源码 | 测试 |
| --- | --- | --- |
| Auth provider 校验和 tag length 边界 | `src/api/xgl_config.c` | `test/test_config.cpp` |
| SECURITY_EXT 编码和 auth trailer 位置 | `src/wire/xgl_frame_auth.c`, `src/wire/xgl_wire_ext.c` | `test/test_wire.cpp`, `test/test_frame.cpp` |
| TTL/header CRC 置零的 canonical AAD | `src/wire/xgl_wire.c` | `test/test_datalink.cpp`, `test/test_network.cpp` |
| Datalink auth verification 和 replay classification | `src/datalink/xgl_datalink_receive.c`, `src/security/xgl_security.c` | `test/test_datalink.cpp`, `test/test_security.cpp` |
