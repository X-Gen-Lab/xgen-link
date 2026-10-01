# ADR 0004：Wire v3 与独立认证安全序号

状态：已采用并实现。Wire v2 被明确拒绝，不自动降级，也不保留短会话兼容路径。

## 版本与编号决策

Wire v3 保持 24 字节基础帧头，并将可靠 DATA 排序编号与认证新鲜性分离。只有可靠 DATA 消耗帧头的 32 位 packet number；ACK、CONTROL 和非可靠 DATA 编码零。SECURITY_EXT 使用 `LE32(key_id) || LE64(security_seq) || u8(tag_length)`，其中 64 位安全序号对每次认证尝试独立递增。

签名前预留序号，provider 失败也不回滚。`UINT64_MAX` 可以使用一次，此后返回序号耗尽。可靠重传保留 DATA 编号和载荷，但重新组合并认证帧，取得新的安全序号。

## Provider 与重放契约

Provider 接收版本化 `xgl_auth_input_t`，包含端点、connection、epoch、key ID、安全序号、规范 nonce、AAD 和载荷。Nonce 为 12 字节：`BE32(已安装的方向性 prefix) || BE64(security_seq)`。AAD 是实际完整帧头与 TLV，仅将 TTL 字节 6、帧头 CRC 字节 22/23 清零；其余全部 AAD 与载荷必须认证。

接收使用 64 位重放窗口。只有验签成功才提交候选窗口，重复安全序号直接拒绝。合法重传使用新安全序号，通过认证后由 DATA 编号去重。转发保留标签，只更新 TTL 和 CRC。认证仅支持单播；策略允许时仍可使用明文广播。

## 信任与恢复边界

应用显式安装精确 `(remote_id, connection_id, session_epoch)` 安全关联，并给出方向性 key、nonce prefix、初始发送序号与最小接收序号。收到 HELLO、SESSION 元数据或有效 CRC 都不能自动安装可信关联。认证帧的关联未知、已关闭或身份不匹配时拒绝处理。明文帧由 `auth_required` 控制；该策略为 false 时，关闭关联不会禁止明文流量。

Transport RESET 不清理安全重放窗口或发送计数。安全 close 保留关闭标记，不回收安全槽，不允许重装同一 scope，也不允许复用该上下文内已预留的 `(key_id, nonce_prefix)` 域。安装时立即预留方向性域，无需先签名；库比较 key ID 而非 provider 的实际密钥，应用必须防止不同 ID 映射到同一实际密钥后复用 prefix。Embedded 提供 4 个安全槽，Full 提供 16 个，Boot 编译裁剪认证。

跨设备重启需要可信新密钥或持久化新鲜性方案。固定 key/prefix 加 RAM 清零不能提供 nonce 唯一性或跨重启防重放。库不实现密钥协商或载荷加密。

## 验证

`test/unit/security/test_security.cpp` 覆盖 nonce/AAD 逐字节向量、每次尝试消耗序号、provider 失败、序号耗尽、重放窗口、关闭标记与 nonce 域拒绝复用。`test/unit/wire/test_wire.cpp` 验证 v3 编码及旧版本拒绝；生产发送与重传路径验证 DATA 编号稳定而认证安全序号变化。
