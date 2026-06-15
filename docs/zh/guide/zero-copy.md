# Zero-Copy

`xgl_send_zerocopy()` 面向单帧、unreliable、调用者拥有 buffer 的发送路径。

## 支持范围

- true zero-copy：单帧 unreliable。
- reliable zero-copy：返回 `XGL_ERR_INVALID_PARAM`；需要 ACK/retry 语义时使用 `xgl_send()`。

## Buffer 要求

- buffer 必须可写。
- payload 前必须预留 header 和必要扩展空间。
- auth_required 时还要预留 SECURITY_EXT 和 auth trailer。
- route MTU 必须容纳最终 frame。

## 偏移规则

未认证单帧发送通常从 `XGL_FRAME_HEADER_SIZE` 开始放 payload。认证路径需要额外 SECURITY_EXT，因此 data offset 必须匹配当前 frame builder 对 header/ext 的要求。不要手写 magic 或 CRC，调用 API 让协议栈填充。

## 所有权

PHY TX 调用返回前 caller buffer 必须保持有效。zero-copy 路径会绕过 transport 和 network 的常规发送处理，在 raw datalink 发送成功后显式补齐两层 TX 统计。

## 常见误用

- 未预留 header 空间。
- 在 `auth_required=true` 时使用不带 tag length 的 provider。
- 把 reliable 数据传给 `xgl_send_zerocopy()`，而不是使用 `xgl_send()`。
