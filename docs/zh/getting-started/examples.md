# 示例

示例位于 `examples/`。

| 示例 | 目的 |
| --- | --- |
| `echo_server` | 演示两个 16-bit 节点之间的基础收发和回显 |
| `multi_node` | 演示三节点路由和转发 |
| `file_transfer` | 演示可靠传输和分片 |
| `platforms` | 演示 bare-metal、FreeRTOS 和 Windows mock port |

构建：

```sh
cmake --preset gcc-test
cmake --build build/gcc-test
```

示例 callback 必须使用 `uint16_t source_id`，与公共 `xgl_rx_callback_t` 保持一致。

`echo_server` 不是单节点 self-loop。它创建 node `1` 和 node `2` 两个协议实例，用两条模拟 PHY 通道连接，避免把生产网络层中拒绝普通 `source_id == target_id` 的规则隐藏掉。
