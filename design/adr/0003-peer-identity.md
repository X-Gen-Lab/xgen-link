# ADR 0003：精确 peer 身份与可靠状态所有权

状态：已采用并实现。

## 决策

生产 peer 的唯一键是 `(remote_id, connection_id, session_epoch)`。零 connection 与零 epoch 都是普通取值，不触发通配或回退。SESSION_EXT 缺省表示 epoch 为零；不再保留短 `session_id`、连接低位推导或节点级隐式匹配。

每个 peer 唯一持有 TX 窗口、可靠队列、RTT、RX 顺序与可选消息状态。ACK 先完整校验 scope、扩展和已发送编号范围，再修改队列、窗口与 RTT。重复有效 ACK 幂等；未来编号、未知 scope 和错误 scope 不完成任何发送。

只有可靠 DATA 消耗连续 32 位 DATA 编号。ACK、CONTROL 与非可靠 DATA 的帧头编号为零。重传从自有逻辑记录重新经过 network/security/datalink，保留 DATA 编号，认证尝试使用独立安全序号。

## 失败、关闭与回收

硬发送错误、重试耗尽、已确认重组过期或精确匹配的 RESET 会终止 scope，释放其数据并报告一次失败。RESET 不创建未知 peer，不清零编号，也不重新开放失败 epoch；重复 RESET 幂等。HELLO 只确保所有者存在，不重置已有状态。

显式 `xgl_close_peer` 释放未认证 peer 的槽；存在已接纳工作时报告一次 CANCELLED。认证 scope 使用 `xgl_close_security_session`，先关闭安全关联再释放 transport，安全关闭标记继续占用槽并保留 nonce 域历史。下一次连接使用新 epoch。未认证模式由应用负责排空旧链路数据，新 epoch 本身不是安全证明。

自动空闲回收只允许从未使用 TX 可靠编号、没有 RX 可靠编号状态且无保留数据的 peer。任一方向存在可靠历史后，peer 保留至显式 close，避免 DATA 编号归零或重复投递；对应 idle 定时器不再产生空唤醒。失败 peer 也必须显式 close。

## 资源与验证

Peer、TX 记录、OOO 包、TX 消息副本与重组字节均有显式全局容量；窗口为一时不要求乱序槽。已 ACK 字节必须由应用接纳或由 transport 持有，临时 BUSY 不允许静默丢弃。静态工作区按资源类别绑定 xgen-core 服务，分配与释放使用同一类别。

`test/test_transport.cpp` 覆盖错误 scope、未来 ACK、RESET 幂等、显式取消、旧 ACK 与新 epoch 隔离、空闲期间双向编号历史以及容量拒绝。`test/property/test_transport_properties.cpp` 使用生产重传路径验证超时与失败生命周期。
