# 资源模型

每个实例拥有一个有界 workspace。协议描述资源归属和容量；可复用的分配算法由独立的 xgen-memory 提供。

## 分配阶段

`xgl_memory_requirements()` 校验配置，返回精确的 `size` 和 `alignment`。布局包括实例、池描述符、初始化对象和全部运行时槽位。

`xgl_init_static()` 准备并初始化调用者提供的对齐存储，不调用 `memory.allocator`。`xgl_create()` 通过配置的 `xgm_allocator_t` 一次性申请整个 workspace，返回尚未初始化的 handle，随后须调用 `xgl_init()`。两条路径共用同一分区计划。

准备完成后，初始化、发送、接收处理及清理都只使用 workspace。`xgl_destroy()` 将动态存储一次性归还原 backend；静态存储仍归调用者所有。借用的配置和上下文必须活到销毁结束。

## 独立资源池

| 资源 | 预留方式 |
| --- | --- |
| 路由和链路 | 固定路由数组；每个独立 PHY 一个 parser cache |
| 路由索引 | embedded/full 预分配 hash 桶和节点；boot 不包含 |
| Peer 和窗口 | 每个 peer 容量一个 peer 对象及窗口存储 |
| 可靠 TX | 按全局 TX 容量分别预留包记录和 payload 槽 |
| 帧 scratch | 同步且不可重入的 I/O 共用一个完整帧槽 |
| 乱序 RX | 按 RX 容量分别预留节点、payload 和扩展槽 |
| 分片 TX | 每个 peer 一个最大消息槽；每个 TX 容量一个扩展槽 |
| 重组 | 每个重组槽一个描述符 |
| 保留的 RX 消息 | 最大消息槽数为重组槽数加 peer 数 |

每类资源拥有独立的 `xgm_pool_t` 服务，小 payload 不能占用 peer 或控制记录的预留容量。释放的槽位可以复用，不回退 heap，也不在运行期扩容。

分片字节预算是共享接纳上限，不是可变大小 arena。固定预留还覆盖应用繁忙时保留的已完成 RX 消息。这种保守预留可能大于配置的并发字节预算。

## 容量和失败

窗口容量属于单个 peer，TX 和乱序容量属于整个实例。耗尽时返回有界失败或延后推进，不自动扩容。可靠分片消息可以超过包窗口：ACK 释放槽位后，再从保留的消息副本生成后续包。

scope 由远端节点、connection ID 和 epoch 标识。恢复时不能在旧 scope 内静默重置序列号。应关闭旧 scope、排空未认证链路旧流量并使用新 epoch；认证模式则关闭可信会话并安装新的可信参数。参见[发送 API](send-api.md)。

## 无 Heap 构建

生产构建使用 `-DXGL_ALLOW_FALLBACK_MALLOC=OFF` 和 `-DXGM_BUILD_LIBC_ALLOCATOR=OFF` 禁止 libc 分配。采用静态初始化，或提供带上下文的显式分配器，只申请一次 workspace。启用 NULL fallback 时，也只在 create 边界解析。

协议的无 heap 结论不涵盖应用驱动和认证 provider，应检查包含这些组件的最终固件链接结果。

## 测量

只有 `requirements.size` 表示总字节数。`runtime_blocks` 是所有类别槽数之和；`runtime_block_size` 是最大槽位步长，两者相乘不能描述实际分区布局。

ABI、profile、路由数、帧大小及全部容量都会影响 RAM。`tools/boot_footprint` 中的 Cortex-M0 工程导出目标布局常量，链接真实 API 路径，并输出 map、栈和堆符号报告。BSP、应用缓冲、ISR 嵌套及完整调用链栈须另外核算。
