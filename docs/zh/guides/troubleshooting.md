# 诊断和调试

先检查返回错误、编译 profile，以及配置对应的精确 workspace 需求。协议不安装内部日志或平台诊断 backend。

## 初始化

检查生成头文件与库的 ABI 是否一致，节点/peer/TX 容量是否有效，路由回调是否齐全，RX 容量是否覆盖完整帧上限。Workspace 地址和大小必须满足 `xgl_memory_requirements()`。

保证配置及引用对象持续有效。`xgl_create()` 只预留存储，还要调用 `xgl_init()`；`xgl_init_static()` 本身包含初始化。无 heap 的动态创建必须提供有效的显式上下文分配器。

## 发送和接收

| 现象或状态 | 检查项 |
| --- | --- |
| `XGL_ERR_WINDOW_FULL` | Peer 在途窗口；继续 step 接收 ACK |
| `XGL_ERR_NO_MEMORY` | Peer/全局包/消息/重组容量及接纳预算 |
| `XGL_ERR_BUSY` | PHY、应用接纳，或 peer 已有待推进消息 |
| `XGL_ERR_ROUTE_NOT_FOUND` | 目标与固定路由配置 |
| `XGL_ERR_BUFFER_TOO_SMALL` | Workspace、RX cache、完整帧开销或消息上限 |
| `XGL_ERR_UNSUPPORTED` | 编译 profile 与请求功能 |
| `XGL_ERR_INVALID_VERSION` | 消费者 ABI/build 标识或接收 wire 版本 |
| `XGL_ERR_ACK_TIMEOUT` | 链路丢失、step 未推进或可靠 scope 已失败 |

确认 PHY RX 无数据时返回零字节，不越过缓冲容量，也不无限阻塞。完整序列化帧须满足两端限制。本地发送成功不是交付回执。

## 诊断和回调

在相同的实例串行化策略下使用 `xgl_stats_get()`、`xgl_stats_reset()` 和应用 `error_callback`。Boot 诊断文本裁剪后可记录数值状态。

回调不能重入或销毁实例，应将恢复动作推迟到返回后。应用 BUSY 不能通过丢弃已被协议接纳到自有存储的数据来“解决”。

## 恢复

可靠 scope 失败后须显式退役并使用新 epoch。未认证 peer 协调排空旧流量后使用 `xgl_close_peer()`；认证会话通过 `xgl_close_security_session()` 关闭，并安装新的可信参数。

只有上板失败时，检查最终链接 map、调用链栈深、驱动半帧发送、DMA 生命周期、中断并发和时间源。通用 footprint ELF 证明的是链接和资源核算，不是板上执行。
