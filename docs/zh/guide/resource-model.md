# 资源模型

XGL 面向有边界的嵌入式系统。生产交付必须说明内存峰值、运行时分配和队列预算。

## 分配阶段

| 阶段 | 说明 |
| --- | --- |
| init | 创建实例、route、parser、reliable、fragment、replay window |
| TX | 普通单帧发送应避免运行时分配 |
| RX | 普通单帧接收应避免运行时分配 |
| reliable | 保存重传数据和队列节点 |
| fragment | 管理分片数组、重组 buffer 和 ranges |

## No-Heap Profile

`XGL_ALLOW_FALLBACK_MALLOC=OFF` 时，NULL allocator 必须 fail closed。`xgl_noheap_smoke` 用于验证严格 profile 行为。

## 预设资源

| Preset | TX Pool | RX Buffer | Window | Max Frame | Fragment |
| --- | ---: | ---: | ---: | ---: | --- |
| Tiny | 1024 | 160 | 2 | 128 | off |
| Small | 2048 | 288 | 4 | 256 | on |
| Medium | 4096 | 544 | 8 | 512 | on |
| Large | 8192 | 1056 | 16 | 1024 | on |
| Production | 8192 | 1056 | 16 | 1024 | on + auth required |

这些是 SDK 起点，不是目标板认证值。最终值必须来自目标链路 MTU、负载大小、窗口、分片并发数和认证 tag 长度。

## Budget 追溯

| Budget | 配置/源码字段 | 验证/测试证据 |
| --- | --- | --- |
| TX pool | `config.memory.tx_pool_size` | `src/api/xgl_config.c`, `test/test_config.cpp` |
| RX buffer | `config.memory.rx_buffer_size` | 必须至少等于 `config.protocol.max_frame_size`；`test/test_config.cpp` |
| Route MTU | `xgl_route_item_t.max_frame_size` | 转发拒绝超大 frame；`test/test_network.cpp` |
| Reliable queue | `config.protocol.window_size`, `config.protocol.max_retry_count` | `test/test_reliable.cpp`, `test/test_window.cpp` |
| Fragment buffers | `xgl_fragment_init()`, `xgl_fragment_set_limits()` | `test/test_fragment.cpp` |
| Auth overhead | `xgl_auth_provider_t.tag_len` 加 SECURITY_EXT | `test/test_datalink.cpp`, `test/test_send.cpp` |

## 生产 Checklist

- allocator 调用计数。
- TX/RX 峰值。
- reliable queue 峰值。
- reassembly budget 峰值。
- stack high-water mark。
- footprint report。

## 运行时确定性

严格生产 profile 的目标是 init 后普通单帧 TX/RX 不触发 allocator。可靠传输和分片可能需要额外池化资源；若目标系统禁止运行时分配，必须用固定池覆盖 reliable packet、rx buffered packet 和 reassembly buffer。
