# 安全

embedded、full profile 编译认证能力，Boot 和 embedded-lite 不包含认证。认证保护单播流量；本实现不提供载荷加密或密钥建立协议。

## 可信关联

广播 ID `0xFFFF` 在安装关联及认证序列化、签名之前都会被拒绝。拒绝不占用安全槽、不消耗安全序号，也不调用 provider 或 PHY。策略允许时仍可使用明文广播。

应用显式安装 `(remote_id, connection_id, session_epoch)`，同时给出方向性的 TX/RX 密钥、nonce 前缀、TX 初始序号和 RX 最小序号。收到 HELLO、SESSION 元数据或通过 CRC 都不会安装信任。认证帧的关联未知、已关闭或不匹配时直接拒绝。全局 `auth_required` 要求所有本地流量认证；扩展选项 `protect_installed_scopes` 要求已安装或已关闭的精确 scope 认证。两者均关闭时，关闭关联不会禁止明文流量。

安装策略作用于后续帧，不撤销或重新认证安装前已经接纳的乱序 DATA。这些数据仍可能在安装后交付。业务需要切换信任边界时，应使用新的可信 epoch/scope，在接收该 scope 流量之前完成安装，并显式处置旧 scope 的已接纳数据。安装不是清空 transport 队列或重建业务会话的替代操作。

关闭安全会话后保留关闭记录。安装时立即预留各方向的 `(key_id, nonce_prefix)` 域。同一安全上下文中，重新安装相同 scope 或复用已预留的域都会被拒绝；关闭槽不会作为全新的安全槽循环使用。容量由构建限制：embedded 为四个，full 为十六个。库比较 key ID，不比较 provider 的实际密钥。应用必须防止不同 ID 映射到同一实际密钥后复用 prefix。跨进程或 MCU 重启需要新的可信密钥或持久化的新鲜度状态；清空 RAM 后继续使用固定密钥和前缀会复用 nonce。

## 认证尝试

SECURITY 值为 `LE32(key_id) || LE64(security_seq) || u8(tag_length)`。provider 接收版本化的 `xgl_auth_input_t`，其中包含实际端点、connection、epoch、安全序号和 12 字节 nonce：`BE32(installed_directional_prefix) || BE64(security_seq)`。

AAD 是实际编码的整个帧头和 TLV，仅将 TTL 第 6 字节及帧头 CRC 第 22–23 字节置零。其余所有 AAD 字节与全部载荷字节都参与认证。标签长度必须等于 provider 声明的固定长度。调用 provider 前先保留新序号；签名失败不回滚。`UINT64_MAX` 可以使用一次，之后关联进入耗尽状态。可靠重传保留 DATA 包号，但每次重新构帧、重新签名。生产发送路径使用已经校验的帧布局定位 SECURITY 和标签，无需再次扫描已编码的 TLV。

## 接收与转发

认证前先校验 CRC 与布局。重放检查作用于 64 位接收窗口的候选副本，只有标签验证成功才提交该副本。重复安全序号会被拒绝，包括字节完全相同的重放。合法重传使用新的安全序号，之后由 transport DATA 包号去重。

Network 在本地交付、修改 transport 状态之前验证安全。转发节点修改 TTL 与 CRC，保留端到端标签；AAD 规范化会排除 TTL。RESET 和 transport close 不重置重放窗口或 TX 安全计数。回调同步执行，不能重入或销毁正在执行的实例。

验证依据：`test/unit/security/test_security.cpp`。
