# 路由

XGL 使用 16-bit 节点地址。本地 `source_id` 不得为 `0` 或
`XGL_BROADCAST_ID` (`0xFFFF`)。收到的 frame 在 source 为 0、source 为
broadcast，或 source 等于 target 且 target 不是 broadcast 时会被拒绝。
`target_id` 可以是本地节点、远端节点或 `XGL_BROADCAST_ID`。

TODO(xgen-link): confirm broadcast/multicast address policy beyond
`XGL_BROADCAST_ID`.

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
6. 更新 TTL，并重算 header/frame CRC。
7. 调用 egress PHY。

## 决策表

| 输入条件 | 决策 | 错误/计数行为 | 证据 |
| --- | --- | --- | --- |
| `source_id == 0` | 丢弃 | `XGL_ERR_INVALID_PARAM`，RX drop count | `src/network/xgl_network.c`, `src/network/xgl_network_receive.c` |
| `source_id == XGL_BROADCAST_ID` | 丢弃 | `XGL_ERR_INVALID_PARAM`，RX drop count | `src/network/xgl_network.c` |
| `source_id == target_id` 且 target 不是 broadcast | 丢弃 | `XGL_ERR_INVALID_PARAM`，RX drop count | `src/network/xgl_network.c` |
| `target_id == local_id` | 本地交付给 transport | 增加 network RX stats | `src/network/xgl_network_receive.c` |
| `target_id == XGL_BROADCAST_ID` | 本地交付给 transport | `xgl_network_is_local()` 视为本地 | `include/xgl/internal/xgl_network.h` |
| 远端 target 且 route 缺失 | 丢弃 | `XGL_ERR_ROUTE_NOT_FOUND`，RX drop count，error callback | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| 远端 target 且 `ttl <= 1` | 丢弃 | `XGL_ERR_TTL_EXPIRED`，RX drop count，error callback | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| 远端 target 且 frame 超过 route MTU | 丢弃 | `XGL_ERR_BUFFER_TOO_SMALL`，RX drop count | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| 远端 target、route 存在、TTL 合法、MTU 适配 | 转发 | 递减 TTL，重算 header CRC 和 frame CRC，调用 route PHY TX | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |

## TTL

TTL 每转发一跳递减。非本地包在 `ttl <= 1` 时返回 `XGL_ERR_TTL_EXPIRED`，不得继续转发；因此成功转发离开本节点时 `ttl >= 1`。

认证帧在 TTL 变化时保留原始端到端 tag。转发路径只更新 TTL 和 CRC 字段；认证仍然有效，因为逐跳可变字段会从 canonical AAD 中排除。

## 交付路径

- 目标为本地节点：交给 transport。
- 目标为其他节点：查 route 并转发。
- broadcast/multicast：地址段保留，第一阶段不作为可靠单播路径。

## 多 PHY 注意事项

每个 PHY 的 `read_freq_hz` 可以不同。`xgl_run()` 会轮询 route，并结合 transport/reassembly deadline 给出下一次唤醒时间。低功耗系统不应为了一个慢速 PHY 固定高频唤醒整个协议栈。

## 路由表内部实现

Route table 使用 hash table 实现 O(1) 查找。

### Hash Table 结构

| 组件 | 说明 |
| --- | --- |
| `xgl_hashtable_t` | 通用 hash table（`src/core/`） |
| 键 | `target_id`（16-bit 节点地址） |
| 值 | route entry（包含 PHY、metric、MTU） |

查找时间复杂度：O(1)（hash + 链式冲突解决）。

### 运行时路由变更

`xgl_route_mutation.c` 支持运行时动态修改路由表：

| 操作 | 说明 |
| --- | --- |
| `add` | 添加新路由条目 |
| `remove` | 移除路由条目 |
| `update_metric` | 更新路由度量值（影响路由选择） |

### Metric 策略

| Metric 类型 | 说明 |
| --- | --- |
| 直连 | metric = 0，最高优先级 |
| 中继 | metric = 跳数，越小越优 |
| 链路质量 | TODO: 确认是否支持 |

### 证据

`src/network/xgl_route.c`, `src/network/xgl_route_lookup.c`, `src/network/xgl_route_mutation.c`, `include/xgl/internal/xgl_route.h`
