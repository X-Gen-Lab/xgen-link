---
id: tutorials/examples
type: tutorial
audience: user
source_language: zh_CN
---

# 示例

## 程序

| Target | 演示 |
| --- | --- |
| `echo_server` | 接收回调排队，下次应用步进发送响应 |
| `file_transfer` | 可靠应用块及 payload 校验 |
| `multi_node` | 经中间节点路由，需要转发能力 |
| `boot_update` | 有界块、慢 Flash BUSY 及丢 ACK 恢复 |

示例通过 `xgl::xgl` 和公开 SDK 头使用协议，应用负责时间和存储。公共同步主机 PHY 把帧复制到有界字节队列。`boot_update` 在应用内编码块位置并校验镜像 CRC，从公开诊断头读取重传计数。

## 运行

显式开启 `XGL_BUILD_EXAMPLES=ON`。运行 CTest 示例还需开启 `XGL_BUILD_TESTS` 或 `XGL_BUILD_NOHEAP_SMOKE`。Boot 不构建转发示例。按[首次收发](first-link.md)运行 echo，其他可执行文件和边界见 `examples/*/README.md`。
