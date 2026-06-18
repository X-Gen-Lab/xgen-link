# 统计系统

XGL 的统计系统提供分层的协议栈运行指标收集能力,帮助生产调试和性能监控。

## 三层统计结构

```text
xgl_statistics_t (实例级聚合)
├── api        (xgl_layer_stats_t — API 层统计)
├── wire       (xgl_layer_stats_t — Wire 层统计)
├── datalink   (xgl_layer_stats_t — Datalink 层统计)
├── network    (xgl_layer_stats_t — Network 层统计)
└── transport  (xgl_layer_stats_t — Transport 层统计)
```

每个 `xgl_layer_stats_t` 包含以下计数器:

| 计数器 | 说明 |
| --- | --- |
| `tx_frames` | 发送帧数 |
| `rx_frames` | 接收帧数 |
| `tx_bytes` | 发送字节数 |
| `rx_bytes` | 接收字节数 |
| `tx_errors` | 发送错误数 |
| `rx_errors` | 接收错误数 |

## 各层扩展统计

### Datalink 层

| 计数器 | 说明 |
| --- | --- |
| `rx_header_crc_errors` | Header CRC 校验失败 |
| `rx_crc16_errors` | Frame CRC16 校验失败 |
| `rx_auth_failures` | 认证验证失败 |
| `rx_replay_duplicates` | 重放拒绝 |

### Transport 层

| 计数器 | 说明 |
| --- | --- |
| `tx_retries` | 重传次数 |
| `tx_ack_timeouts` | ACK 超时次数 |
| `tx_window_full` | 窗口满拒绝次数 |

### Network 层

| 计数器 | 说明 |
| --- | --- |
| `rx_forwarded` | 转发帧数 |
| `rx_ttl_expired` | TTL 过期丢弃数 |
| `tx_route_not_found` | 路由未找到 |

## API

### 查询统计

```c
xgl_statistics_t stats;
xgl_get_statistics(instance, &stats);

// 访问各层统计
printf("TX frames: %lu\n", stats.datalink.tx_frames);
printf("CRC errors: %lu\n", stats.datalink.rx_crc16_errors);
```

### 分层统计指针

每层通过 `xgl_layer_stats_t*` 直接更新统计,避免聚合时的锁开销:

```text
xgl_transport_ctx_t.stats → &instance->statistics.transport
xgl_datalink_ctx_t.stats  → &instance->statistics.datalink
xgl_network_ctx_t.stats   → &instance->statistics.network
```

## 运行时阶段统计

Tracking allocator 提供按阶段的内存分配统计:

| 阶段 | 说明 |
| --- | --- |
| INIT | 实例创建和初始化期间的分配 |
| TX | 稳态发送路径的分配 |
| RX | 稳态接收路径的分配 |
| RELIABLE | 可靠重传存储的分配 |
| FRAGMENT | 分片和重组的分配 |

```c
xgl_allocator_phase_stats_t phase_stats;
xgl_tracking_allocator_get_phase_stats(tracker, &phase_stats);

// 查看每个阶段的峰值内存使用
for (int i = 0; i < XGL_ALLOCATOR_PHASE_COUNT; i++) {
    printf("Phase %d peak: %zu bytes\n", i,
           phase_stats.phase[i].peak_allocated);
}
```

## 编译时控制

`XGL_ENABLE_STATISTICS` (默认 y):

- `y`: 启用统计收集,极小开销。
- `n`: 禁用统计,所有计数器编译时消除。

## 证据

| 规则 | 源码 | 测试 |
| --- | --- | --- |
| 统计聚合 | `src/api/xgl_stats.c` | `test/test_stats.cpp` |
| 分层统计 | `src/api/xgl_stats.c` | `test/test_layered_stats.cpp` |
| Tracking allocator 阶段统计 | `src/memory/xgl_tracking_allocator.c` | `test/test_allocator.cpp` |
| Tiered pool 统计 | `src/memory/xgl_tiered_pool_stats.c` | `test/test_tiered_pool.cpp` |