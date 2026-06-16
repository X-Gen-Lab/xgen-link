# 路由

XGL 使用 16-bit 节点地址。`0` 和保留地址不能作为普通本地节点 ID。

## Route Table

每个 route 绑定：

- `target_id`
- PHY callbacks
- `max_frame_size`
- `read_freq_hz`
- metric

转发前必须检查 route MTU。超过 `max_frame_size` 的帧应返回 `XGL_ERR_BUFFER_TOO_SMALL` 并丢弃。

## 路由决策顺序

1. 检查 frame 是否目标为本地节点。
2. 如果本地，交给 transport。
3. 如果不是本地，检查 TTL。
4. 查找 `target_id` 对应 route。
5. 检查完整 frame 长度不超过 route MTU。
6. 更新 TTL 和必要的 header/auth/CRC 边界。
7. 调用 egress PHY。

## TTL

TTL 每转发一跳递减。非本地包在 `ttl <= 1` 时返回 `XGL_ERR_TTL_EXPIRED`，不得继续转发；因此成功转发离开本节点时 `ttl >= 1`。

## 交付路径

- 目标为本地节点：交给 transport。
- 目标为其他节点：查 route 并转发。
- broadcast/multicast：地址段保留，第一阶段不作为可靠单播路径。

## 多 PHY 注意事项

每个 PHY 的 `read_freq_hz` 可以不同。`xgl_run()` 会轮询 route，并结合 transport/reassembly deadline 给出下一次唤醒时间。低功耗系统不应为了一个慢速 PHY 固定高频唤醒整个协议栈。
