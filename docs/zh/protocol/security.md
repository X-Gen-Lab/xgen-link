# 安全模型

生产配置默认要求认证。测试和调试可以显式关闭，但 release profile 应启用 `auth_required`。

## Auth Provider

`xgl_auth_provider_t` 提供：

- `sign`
- `verify`
- `tag_len`
- `user_data`

当 `auth_required=true` 且 provider 缺失或 `tag_len == 0` 时，初始化必须失败。

## AAD 和 Payload

基础头和扩展头作为 AAD。payload 被认证但不加密。CRC 用于快速误码检测，认证 tag 用于防伪造、防篡改和防重放。

## 认证 Trailer

认证 trailer 位于 payload 之后、frame CRC 之前。SECURITY_EXT 记录 `key_id`、`nonce_id` 和 `tag_len`，实际 tag 长度由 provider 固定声明，避免签名路径先试算再重签。

## 验证顺序

1. 检查 magic、version、header_len、payload_len。
2. 验证 header CRC。
3. 解析 SECURITY_EXT。
4. 验证 auth trailer。
5. 检查 replay window。
6. 进入 network/transport 语义处理。

任何一步失败都不得交付 payload。

## Replay Window

反重放 key：

```text
source_id + connection_id + session_epoch + packet_number
```

重复包、旧 session 包和错误 connection 包不得 ACK 或交付。

## 多跳转发

TTL 是每跳可变字段。转发路径在修改 TTL 后必须保证下一跳校验能通过：要么重算 hop-level 认证边界，要么使用明确的 mutable header 策略。当前实现要求转发路径重新生成必要的 CRC/认证材料。

## Reserved

加密能力当前保留。不要把 `enable_encryption` 当作可用生产加密路径。

## 密钥边界

XGL 不持久化密钥，也不规定密钥派生方案。生产应用应在 auth provider 内部完成密钥存储、轮换、key id 映射和硬件安全模块接入。
