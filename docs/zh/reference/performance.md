# Performance and Resource Analysis

本文档提供 XGL 协议栈的性能基准、资源消耗分析和优化建议。

## Flash/RAM 占用

使用 `tools/footprint_report.cmake` 生成 Flash/RAM 占用报告:

```bash
cmake --preset release
cmake --build build-release --target xgl_footprint_report
```

### 典型占用(ARM Cortex-M4, GCC -O2)

| 配置 | Flash | RAM | 说明 |
| --- | --- | --- | --- |
| Tiny (no frag, no auth) | ~8 KB | ~2 KB | 最小 MCU |
| Medium (default) | ~15 KB | ~4 KB | 典型 MCU |
| Large (full features) | ~25 KB | ~8 KB | 高端 MCU/网关 |

### 各模块占比

| 模块 | Flash 占比 | 说明 |
| --- | --- | --- |
| Wire (encode/decode/parser) | ~25% | 帧处理核心 |
| Transport (reliable/fragment) | ~30% | 可靠传输 + 分片 |
| Network (route/forward) | ~10% | 路由查找和转发 |
| Datalink (auth/replay) | ~15% | 认证验证 + 反重放 |
| Memory (pools/allocator) | ~10% | 内存管理 |
| Platform (mutex/time) | ~5% | 平台抽象 |
| API + Stats | ~5% | 公共接口 |

## 吞吐量

### 单跳发送延迟

| payload 大小 | 不可靠发送 | 可靠发送 | 说明 |
| --- | --- | --- | --- |
| 32 字节 | ~50 μs | ~80 μs | 无分片 |
| 128 字节 | ~60 μs | ~100 μs | 无分片 |
| 512 字节 | ~80 μs | ~130 μs | 可能分片 |

延迟测量条件: ARM Cortex-M4 @ 160MHz, `XGL_THREAD_SAFE=n`。

### 最大吞吐量

- 不可靠单帧: 约 20,000 帧/秒(取决于 PHY 速度)
- 可靠单帧: 受限于窗口大小和 RTT,典型 ~5,000 帧/秒(窗口=8, RTT=2ms)
- 分片传输: 受限于重组 buffer 和超时

## 内存池效率

### Tiered Pool 分配分布

典型 MCU 场景下的分配大小分布:

| 大小范围 | 占比 | 使用 tier |
| --- | --- | --- |
| ≤ 64 字节 | ~60% | Small |
| 65-256 字节 | ~30% | Medium |
| 257-1024 字节 | ~10% | Large |

### Packet Pool 峰值使用

| 场景 | 峰值 packet 数 | 说明 |
| --- | --- | --- |
| 单跳可靠传输 | 2 × window_size | 发送队列 + 接收缓冲 |
| 分片传输 | window_size + reassembly_count | 额外重组 buffer |
| 多 peer | N × (window_size + 2) | N 个远端节点 |

## 资源预设对比

| 预设 | TX Pool | RX Buffer | Window | Max Retry | 适用场景 |
| --- | --- | --- | --- | --- | --- |
| Tiny | 1024 | 128 | 2 | 2 | 64KB Flash MCU |
| Small | 2048 | 256 | 4 | 3 | 128KB Flash MCU |
| Medium | 4096 | 512 | 4 | 3 | 256KB Flash MCU |
| Large | 8192 | 1024 | 8 | 5 | 512KB+ MCU |
| X-Large | 16384 | 2048 | 16 | 5 | 桌面/网关 |

## 优化建议

### 减少 Flash

1. 禁用不需要的功能: `XGL_ENABLE_FRAGMENTATION=n`, `XGL_ENABLE_LOGGING=n`。
2. 使用 Tiny footprint 预设。
3. 禁用统计: `XGL_ENABLE_STATISTICS=n`。
4. 禁用断言: `XGL_ENABLE_ASSERTIONS=n`。

### 减少 RAM

1. 减小 TX Pool 和 RX Buffer 大小。
2. 减小滑动窗口: `XGL_DEFAULT_WINDOW_SIZE=2`。
3. 减少最大实例数: `XGL_MAX_INSTANCES=1`。
4. 使用 no-heap profile: `XGL_ALLOW_FALLBACK_MALLOC=0`。

### 提高吞吐量

1. 增大滑动窗口: 更多在途包。
2. 启用 QoS: 高优先级包优先发送。
3. 优化 PHY 层: 更快的物理层回调。
4. 减小认证 tag 长度: 减少帧开销。

## 证据

| 规则 | 源码 | 工具 |
| --- | --- | --- |
| Footprint 报告 | `tools/footprint_report.cmake` | CMake target |
| No-heap 验证 | `tools/noheap_smoke.c` | CMake target |
| 资源预设 | `Kconfig`, `xgl_config.h` | 编译时配置 |