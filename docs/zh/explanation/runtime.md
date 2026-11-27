# 接收与运行时工作预算

`XGL_ENABLE_ADVANCED_RUNTIME=ON` 启用可续跑的接收校验和
`xgl_step_bounded()`。这是显式构建选项，所有 profile 默认均为 OFF。关闭时保留
原 parser 布局，有界入口返回 `XGL_ERR_UNSUPPORTED`。库和消费者必须使用一致的
选项，以生成的构建配置为准。

## 预算计量对象

已有 `xgl_work_budget_t` 控制每个 PHY 新读取的字节数和 parser 超时。
`xgl_execution_limits_t` 补充两个独立上限：

| 字段 | 范围和含义 |
| --- | --- |
| `parser_candidates` | 每条链路、每次 step 的校验机会数 |
| `tx_packets` | 整个 transport 维护阶段、每次 step 的报文提交尝试次数 |

零禁止对应工作，`SIZE_MAX` 取消该项限制。定时维护仍执行。新输入字节使固定头、
扩展块或完整帧达到可检查状态时扣除一个 parser 配额；一次检查可以完成多个校验
阶段。恢复扫描中，每次候选检查扣除一个配额；扫描结束后恢复最早半帧，也需要
一次检查。扫描噪声和推进已消费的存储不消耗配额。

这里限制校验尝试次数，不限制 CRC 字节数或 CPU 周期。单次检查仍可能校验整个
帧。PHY 回调、认证 provider、应用回调、内存移动和扫描均有执行成本。接收触发的
ACK 和应用显式发送不受维护阶段 TX 配额限制。产品应包含这些成本，测量完整 step
的最坏执行时间。

有界维护在处理到期重传时按 peer 轮换起点，为各 peer 提供提交机会。
回调耗时、驱动背压和实际服务时延仍需产品验证。

## 续跑与输入所有权

有界 step 返回首个处理错误，并独立写入 `result.pending`，调用方应同时检查。
`pending` 表示配额阻止本地可执行工作完成，不表示猜测驱动中可能还有未读字节。
应由同一个串行 owner 使用正配额续跑。应用交付返回 BUSY 时，需要应用容量恢复
事件，不能用紧密循环不断重试。

接收路径复用原 parser cache，同时保存正在解析的候选和本次读入但尚未处理的
尾部。PHY 只写入暴露的空闲 cache，每次 poll 最多读取一次。所有已接纳字节在
返回前均已处理或持久保留。恢复暂停不会把未读尾部留在临时栈数组，也不会新增
一个 MTU 大小的缓冲区。

内部 span 入口返回 `consumed`，可能小于提供的长度。调用方必须保留并准确重传
未消费后缀，已消费前缀的副本归 parser 管理。输入不能与 parser 存储重叠。
产品通常使用公开 step API 和同步 PHY 契约，无需调用这个私有函数。

解码后的帧 view 借用 parser 存储。同步交付完成后，parser 才能压缩或覆盖该
存储。回调要保存后续使用的数据时必须复制，不得重入、重置或销毁实例。即使上层
拒收，完整且 wire 合法的帧仍被消费，其 payload 不会被解释为新候选。

`rx_bytes=0` 禁止新的 PHY 读取，仍可推进已保留的 parser 工作和 transport
维护。启用 ADVANCED_RUNTIME 后，普通 `xgl_step()` 以无限候选和维护配额排空
同一份状态，因此两种入口交替使用不会丢失输入；切换回普通入口也会取消这些
工作限制。

## 恢复与时间

拒绝一个帧后，恢复扫描检查后续 magic 位置，优先选择最早的完整合法帧；没有
完整帧时，保留最早可能成立的半帧。预算耗尽会保存扫描游标和半帧位置，续跑
不需要从被拒绝的头部重新扫描。扫描中的失败候选不会重复报告原始 CRC 错误。

保留后缀保守地继承原候选的起始时间。未处理输入批次中新开始的候选使用该批次
读入时刻，不使用较晚的续跑时刻。预算暂停不会刷新已有帧的绝对截止时间。
Parser 超时或重置会同时清除恢复状态和保留的输入批次，这是明确的丢弃边界。

所有操作使用同一个单调毫秒时钟，时间间隔小于 `2^31` 毫秒。接收超时为零时关闭
到期清理；候选和 RX 配额均为零时仍执行超时维护。策略和截止时间查询详见
[低功耗运行时](../guides/low-power.md)。

## 轮询与吞吐

对于到期链路，一次 PHY 读取最多接纳：

```text
B = min(rx_bytes, XGL_DATALINK_RX_CHUNK_SIZE, free_parser_cache_bytes)
```

若每 `T` 秒读取一次，`B / T` 是以字节/秒表示的理想接收上限，不是服务速率保证。
恢复暂停、驱动短读、候选配额不足和 cache 占用都会进一步降低吞吐。当配额充足且
`B` 恒定时，长度为 `L` 的帧至少需要 `ceil(L / B)` 次读取。RX 事件不会绕过
已经配置的 PHY 轮询间隔。
只有实际调用 PHY RX 才会推进该间隔，驱动返回错误也计入一次轮询。仅续跑
暂停的 parser 而未读取 PHY 时，不会延后下一次允许读取的时刻。

例如每 1 ms 轮询、chunk 为 32 字节时，忽略解析、调度和驱动成本的读取上限为
32,000 字节/秒。把 `rx_bytes` 增加到 32 以上不会增大该构建的 chunk。驱动环形
缓冲应覆盖真实突发和 owner task 最长服务间隔；parser 超时应允许接收并检查
最大合法帧所需的时间。

## DMA 与 Owner Task 接入

DMA 所有权由板级驱动维护，常见路径为：

```text
DMA -> 驱动拥有的环形缓冲 -> 同步 PHY RX 复制
    -> parser cache -> owner task 中的协议和应用回调
```

DMA ISR 记录生产进度并唤醒 owner，不调用协议 API。驱动必须结合真实平台处理
回绕、DMA 缓存一致性、生产进度发布和覆盖检测。PHY RX 只把稳定可用的字节复制
到传入缓冲，返回实际长度，不能保留该指针。RX 返回后 parser 存储可能立即移动，
不能作为长期 DMA 目标地址。另见[移植指南](../guides/porting.md)。

以下 owner 单次迭代展示控制流程。所有 `product_*` 函数均为产品提供的适配占位符，
不是 SDK API，也不代表已经通过验证的板级适配：

```c
void product_link_iteration(xgl_handle_t handle) {
    const xgl_work_budget_t budget = {128U, 100U};
    const xgl_execution_limits_t limits = {4U, 2U};
    xgl_execution_result_t result;
    xgl_error_t error = xgl_step_bounded(
        handle, product_monotonic_ms(), &budget, &limits, &result);
    product_report_step_result(error);
    if (result.pending) {
        product_schedule_owner_again();
        return;
    }
    uint32_t delay_ms;
    bool timed = xgl_next_timeout_for_work(
        handle, product_monotonic_ms(), &budget, &delay_ms);
    product_wait_for_rx_capacity_or_timeout(timed, timed ? delay_ms : 0U);
}
```

调度必须给其他产品工作执行机会。事件注册和等待应避免竞态，容量恢复通知也应
唤醒同一个 owner。若某项配额持续为零且存在 pending，需要修改策略或等待外部
事件；反复使用相同的零配额调度不能排空这些工作。

## 验证范围

主机回归覆盖所有输入切分位置、随机 chunk 和配额（seed `5785420`）、嵌套候选、
最早半帧恢复、源缓冲被修改后的输入保留、cache 容量、超时/重置和切回普通接收
入口，并检查特性开启及关闭两种 parser 配置。这些测试验证字节守恒与状态机行为，
不构成板级吞吐、DMA 正确性或最坏时延结论。

产品验收还应覆盖持续输入、密集损坏数据、驱动环形缓冲溢出、长回调、DMA 回绕
和缓存操作、时钟回绕以及多条活动链路的公平性。记录所选特性/profile 组合的
完整镜像 Flash、RAM 和栈测量结果。
