# 可靠传输

每个 peer 独占一个 TX 窗口、可靠队列和 RX 排序状态；启用自适应 RTO 时还拥有 RTT 估计器。唯一键为 `(remote_id, connection_id, session_epoch)`，没有实例级回退窗口，也没有独立的 PHY 直接重传通道。

## 发送生命周期

可靠 DATA 在提交前复制。接纳失败不会消耗包号。下层首次发送成功后，将保留的包标记为已发送，时间戳零也是合法值，然后推进 DATA 包号。ACK 完成释放队列记录，只推进所属 peer 的窗口。

首次发送 HELLO 前，transport 先检查帧与消息长度、scope 状态、编号空间、窗口和全局 TX 容量，并预留可靠记录与载荷。可靠分片还先预留完整消息副本和首片的记录、载荷及扩展；本地接纳失败不发送 HELLO。HELLO 成功并不保证 DATA 已到达远端，接收端仍需要未使用预约的有界生命周期。

初始 ACK 超时优先使用请求中非零的 `timeout_ms`。没有单次覆盖时，已有有效 RTT 样本的 peer 使用估算 RTO；尚无样本时使用配置的 `protocol.ack_timeout_ms`。RTT 先用宽位中间值计算，再限制结果范围，包含合法 32 位毫秒样本的最大值。

构建时选择 `XGL_ENABLE_ADAPTIVE_RTO=OFF` 后，peer 不保存 RTT 估计器，ACK 不采集 RTT 样本；没有单次覆盖时始终使用配置的 ACK 超时。单次 `timeout_ms` 覆盖、重试次数和指数退避仍生效。该选项只改变超时选择，不改变 ACK/SACK 或 wire 格式；编译资源与 SDK 身份见[构建与测试](../../../guides/build-and-consume.md)。

超时重传与 SACK 重传使用同一 transport 过程，再次经过 network 和 datalink。DATA 包号和载荷保持不变，认证获得新的安全序号。暂时 BUSY 或 NO_MEMORY 会保留已接纳字节，并安排正的重试延迟：默认超时限制到 1–100 ms。重传成功更新时间戳、次数和指数退避，退避上限为 30000 ms。下层硬错误或次数耗尽使整个 peer 失败，并报告一次。

ACK 处理负责校验反馈并释放已确认记录。SACK 只标记允许快重传的缺口，不在接收调用链中递归发送 DATA。同一次或后续 `xgl_step()` 的 transport 维护阶段执行重传，并在 ACK 释放窗口后推进已接纳的分片消息。可执行工作参与 `xgl_next_timeout()`；已安排的本地容量重试仍保持正延迟。

SACK 只为尚未成功重传的待确认包请求快重传。一旦任意重传成功，该记录后续恢复由 RTO 驱动，重复反馈不会消耗剩余重试预算；新反馈仍可调度其他尚未重传的缺口。BUSY 不消耗次数。每次成功重传都在提交点更新聚合计数，即使后续缺口遇到失败或背压，也不遗漏已完成的重传。

## 接收与确认

顺序 DATA 只有在应用接纳或 transport 已持有数据后才确认。应用返回 BUSY 时，尚未保留的顺序包不确认。乱序包复制成功后才能 SACK；一旦持有，临时应用背压不会丢弃它。重复包不再次交付。重传过的包不用于 RTT 采样。

本地 transport 只接纳 DATA、ACK 和 CONTROL 报文类型。可靠广播在 peer 接纳前拒绝，不能消耗单播 scope 的接收编号。可靠 DATA 的 `UINT32_MAX` 为保留编号，在缓存或确认前拒绝。耗尽的 scope 不会将接收历史回绕到零。

分片元数据在乱序缓存或 SACK 前校验：必须恰有一个格式正确的 FRAGMENT 扩展，载荷范围有效，总长度处于支持范围。同一 scope 中，同一活跃 `message_id` 的可靠性、应用类型和总长度必须一致；检查同时覆盖未完成重组和已持有的乱序分片，在 ACK/SACK 前拒绝冲突，且不修改原有字节。结构合法的未知 TLV 保持兼容。

ACK 或 SACK 发送失败会返回错误，但不回滚已接纳的载荷、接收编号或保留字节。重组记录中的可靠属性来自实际接纳，ACK 提交失败不能撤销它。连续的自有缓存仍继续处理，并保留首个错误。对已接纳 DATA 的重传可以再次 ACK，不会重复交付应用。

首包拒绝不提交可靠 RX 历史。如果本次接收创建了新 peer，但没有接纳数据且未使 scope 失败，立即归还其预留槽，关闭空闲回收时也一样。已有 peer、已接纳或保留数据的 scope，以及失败身份均保留。该回滚不得清除已确认历史或认证重放状态。

ACK 精确匹配 scope，先校验完整 TLV 流和确认范围，再修改状态；不能完成未发送或未来的包。重复有效 ACK 幂等。ACK 帧头包号不表示确认目标。

## 失败与恢复

HELLO 确保 peer 存在，不重置已有状态。仅由 HELLO 预留且仍未使用的空 peer 有独立的 5000 ms 租期，从首次接收时刻开始，重复 HELLO 不续期。租期参与 `xgl_next_timeout()`；到期后，调用方提供显式时间的 transport 维护会回收空预约，即使 `peer_idle_timeout_ms=0`。普通空闲回收期限更早时可提前回收。已使用可靠编号、已失败或仍拥有数据的 scope 不因 HELLO 租期回收。

RESET 只作用于已存在的精确 scope；首次将其终止并报告取消，重复请求不产生额外状态转换或错误回调。未知 RESET 不分配 peer。

重组超时按消息自身的接纳属性处理。纯非可靠消息超时只释放该消息并返回 TIMEOUT，不终止曾有可靠 RX 历史的 scope，也不清除其无关可靠 TX。拥有已接纳可靠字节的消息超时则终止所属 scope，并报告一次；即使其 ACK 曾提交失败，也保持这一规则。过期记录先从活动队列摘除，再通知 scope 所有者与释放存储，避免 scope 清理使过期遍历失效。

失败的可靠 scope 不能继续接纳新的可靠工作。`xgl_close_peer` 释放未认证 scope；如仍有已接纳工作，则报告一次 CANCELLED。认证模式关闭 security session，同时退休 transport 所有权，但保留 nonce 域的关闭记录。重新连接必须建立新 epoch。无认证模式要求调用方排空旧链路数据；仅选择 epoch 不构成安全机制。

Transport 维护优先返回首个硬错误，同时继续维护其他 scope；没有硬错误时，返回首个即时反馈失败。重试已持有工作时遇到 BUSY 或 NO_MEMORY，会保留工作，视为正常维护背压；这不隐藏即时接收接纳或 ACK 提交错误。缓存已交付后的 ACK BUSY/NO_MEMORY 不属于仍持有数据的背压，维护会返回该错误。完整待交付消息在应用回调后来返回硬错误时终止所属 scope，释放自有字节并只报告一次。

Scope 任一方向使用过可靠编号后，空闲时间不能回收其历史。仅未使用且无保留数据的 peer 可以自动回收，已使用 scope 必须显式 close。

验证依据：`test/unit/transport/test_reliable.cpp`、`test/unit/transport/test_reliable_owned.cpp`、`test/unit/transport/test_transport.cpp`、`test/unit/transport/test_transport_rx_contracts.cpp`、`test/unit/transport/test_transport_fragment_scope.cpp`、`test/unit/transport/test_transport_runtime_contracts.cpp`、`test/unit/transport/test_transport_fixed_rto.cpp` 与 `test/integration/test_transport_admission.cpp`。
