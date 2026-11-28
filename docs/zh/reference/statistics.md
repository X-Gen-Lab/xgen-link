---
id: reference/statistics
type: reference
audience: user
source_language: zh_CN
---

# 统计

`xgl_statistics_t` 分别记录 datalink、network、transport 计数器，以及重传、CRC 错误和 RTT 指标。

## 解释

同一个包可能计入多个协议层，不能把各层字节数相加作为应用吞吐量。提交成功和 transport TX 计数不证明远端应用已经交付；应在接收回调测量已接纳的 payload。

普通、原地和转发路径的 network、datalink TX 包数及字节数均在下层成功提交后累计。Network 字节表示载荷，datalink 字节包含帧开销；失败不计为已发送包。每次成功重传即累计重传计数，即使同一批 SACK 的后续重传失败也保留已完成计数。没有触发再次提交的重复 SACK 不增加计数。

流式 parser 和原始帧入口使用相同的 CRC 分类：帧头 CRC 与完整帧 CRC 分别累加对应错误计数，并同步报告 `XGL_ERR_CRC_FAILED`。分类使用当前验证的结果，不为统计再次计算 CRC。

流式恢复只记录最初拒绝候选的诊断；扫描缓存时遇到的假 magic 不重复计错。恢复出的每个完整帧分别计入接收包数和完整帧字节数。上层拒绝已通过 wire 校验的帧不会触发载荷重新同步。

Datalink 接收统计区分帧校验与驱动调用：wire 格式/CRC 拒绝计入 `rx_errors`，CRC 另按类型报告；普通 wire 格式拒绝不额外调用错误回调，超出 datalink 帧长策略则报告 `XGL_ERR_INVALID_FRAME`。半帧超时重置并报告一次。PHY `rx` 回调自身返回的错误原样交给 step 调用方，不计作帧错误，也不重复触发协议错误回调；驱动故障计数由适配层维护。完整有效帧在调用上层前已计入 datalink 接收量，上层 BUSY 不回退该计数。TX PHY 失败则计入 datalink `tx_errors` 并报告；各方向的现有含义保持明确，不把多个层次错误相加作为故障次数。

## 发送受理边界

普通或零拷贝发送遇到 PHY 失败时，本次提交经过的各层分别增加一次 TX 错误。这包括驱动返回 `XGL_ERR_BUSY`、`XGL_ERR_TX_FAILED` 或 `XGL_ERR_INVALID_PARAM`。各层只把成功提交计入已发送包数和字节数。

对零拷贝发送，发送尝试从基础描述符校验通过、进入统一 network 发送路径前开始。空指针、零载荷长度、无效优先级和基础缓冲边界错误均在此前拒绝，不增加 TX 错误。受理之后，路由、MTU、精确头部空间或下层提交失败会增加 transport `tx_errors`；下层仍按各自校验与提交阶段计数。不能仅凭错误码区分调用参数错误和驱动返回的错误。布局查询无论成功或失败均不构成发送尝试，详见[零拷贝](../guides/in-place-send.md)。

这里的受理只表示已经开始处理逻辑发送，不代表 PHY 接纳了帧、可靠消息已入队或对端已经收到。

## Network 计数

下表仅说明 network 层计数。表中未列出的计数器不会因该事件更新；datalink 和 transport 可以独立记录各自处理过程。

| 事件 | Network 计数变化 |
| --- | --- |
| 本地帧通过地址与安全策略检查 | 调用 transport 前 `rx_packets += 1`、`rx_bytes += payload_len` |
| Transport 拒绝已受理的本地帧，包括 BUSY | 不回退 network RX 计数，也不增加 network 错误 |
| 本机发送或转发的帧被下层接纳 | `tx_packets += 1`、`tx_bytes += payload_len` |
| 转发的帧被下层接纳 | 不增加 network RX 包数或字节数；这些 RX 计数表示本地受理 |
| 地址或本地安全策略拒绝帧 | `rx_dropped += 1` |
| 转发关闭、没有转发路由、TTL 到期或超出出口 MTU | `rx_dropped += 1` |
| 转发临时缓冲分配失败 | `rx_dropped += 1`、`tx_errors += 1` |
| 转发进入下层后提交失败 | `tx_errors += 1`；不增加已发送包数、字节数或 RX 丢弃计数 |
| 本机逻辑发送在路由、组帧、预算检查或下层提交时失败 | `tx_errors += 1` |
| 内部完整帧入口因 wire 编码或实例帧长限制拒绝帧 | `rx_errors += 1`；不增加 `rx_dropped` |

公开流式入口通常在 datalink 层拒绝非法 wire 帧，这些帧不会进入 network。最后一行说明独立的内部完整帧入口，不表示每次 datalink 拒绝还要额外增加 network 错误。转发节点校验 wire 帧，不在本地认证过境载荷；最终端点执行自身的安全策略。

## 访问

包含 `<xgl/xgl_diagnostics.h>` 后可调用 `xgl_stats_get()` 和 `xgl_stats_reset()`。调用时应用必须独占实例。协议不提供内部 mutex 或原子快照保证。重置计数器不会重置 peer、包序号、会话或待处理数据。

## 编译配置

Full、Embedded 和 Embedded-lite 启用 `XGL_FEATURE_STATISTICS`。Boot 关闭统计对象及计数更新；有效实例上的 `xgl_stats_get()` / `xgl_stats_reset()` 返回 `XGL_ERR_UNSUPPORTED`，获取函数不改写输出。空指针和未初始化检查仍先执行。

## RTT 和内存

RTT 只统计未重传包的有效 ACK 样本，零时间差和超出有符号时间范围的样本不参与。平均值为整数毫秒；尚无样本时平均值和最大值为零，最小值为 `UINT32_MAX`。采样计数达到 `UINT32_MAX` 后停止累加观察值，协议自身的 RTO 估计仍独立更新。

`memory_used` 表示 `xgl_memory_requirements()` 为实例测得的完整 workspace 预留量，包含初始化区和各资源池；它不是载荷池当前活跃字节数。静态和分配器初始化都使用固定 workspace，因此 `memory_peak` 等于该预留量。重置统计保留这两个值，清空 RTT 观察值与各层计数；它不改变 RTT/RTO 控制状态。
