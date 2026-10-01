# 可靠传输

每个 peer 独占一个 TX 窗口、可靠队列、RTT 估计器和 RX 排序状态。唯一键为 `(remote_id, connection_id, session_epoch)`，没有实例级回退窗口，也没有独立的 PHY 直接重传通道。

## 发送生命周期

可靠 DATA 在提交前复制。接纳失败不会消耗包号。下层首次发送成功后，将保留的包标记为已发送，时间戳零也是合法值，然后推进 DATA 包号。ACK 完成释放队列记录，只推进所属 peer 的窗口。

超时重传与 SACK 重传使用同一 transport 过程，再次经过 network 和 datalink。DATA 包号和载荷保持不变，认证获得新的安全序号。暂时 BUSY 或 NO_MEMORY 会保留已接纳字节，并安排正的重试延迟：默认超时限制到 1–100 ms。重传成功更新时间戳、次数和指数退避，退避上限为 30000 ms。下层硬错误或次数耗尽使整个 peer 失败，并报告一次。

## 接收与确认

顺序 DATA 只有在应用接纳或 transport 已持有数据后才确认。应用返回 BUSY 时，尚未保留的顺序包不确认。乱序包复制成功后才能 SACK；一旦持有，临时应用背压不会丢弃它。重复包不再次交付。重传过的包不用于 RTT 采样。

ACK 精确匹配 scope，先校验完整 TLV 流和确认范围，再修改状态；不能完成未发送或未来的包。重复有效 ACK 幂等。ACK 帧头包号不表示确认目标。

## 失败与恢复

HELLO 确保 peer 存在，不重置已有状态。RESET 只作用于已存在的精确 scope；首次将其终止并报告取消，重复请求不产生额外状态转换或错误回调。未知 RESET 不分配 peer。

失败的可靠 scope 不能继续接纳新的可靠工作。`xgl_close_peer` 释放未认证 scope；如仍有已接纳工作，则报告一次 CANCELLED。认证模式关闭 security session，同时退休 transport 所有权，但保留 nonce 域的关闭记录。重新连接必须建立新 epoch。无认证模式要求调用方排空旧链路数据；仅选择 epoch 不构成安全机制。

Scope 任一方向使用过可靠编号后，空闲时间不能回收其历史。仅未使用且无保留数据的 peer 可以自动回收，已使用 scope 必须显式 close。

验证依据：`test/test_reliable.cpp` 与 `test/test_transport.cpp`。
