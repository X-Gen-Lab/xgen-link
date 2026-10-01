# 示例

## 程序

| Target | 演示 |
| --- | --- |
| `echo_server` | 接收回调排队，下次应用步进发送响应 |
| `file_transfer` | 可靠应用块及 payload 校验 |
| `multi_node` | 经中间节点路由，需要转发能力 |
| `boot_update` | 有界块、慢 Flash BUSY 及丢 ACK 恢复 |

全部示例使用公开 API、显式时间和固定应用存储。公共同步主机 PHY 把帧复制到有界字节队列。

## 运行

开启 `XGL_BUILD_EXAMPLES=ON`，构建后运行 CTest。Boot 不构建转发示例。可执行文件路径及边界见 `examples/*/README.md`。
