---
id: explanation/resource-model
type: explanation
audience: user
source_language: zh_CN
---

# 资源模型

每个实例拥有一个有界 workspace。协议用私有固定池管理独立资源类别及容量。

## 分配阶段

`xgl_memory_requirements()` 校验配置，返回精确的 `size` 和 `alignment`。布局包括实例、池描述符、初始化对象和全部运行时槽位。

`xgl_init_static()` 准备并初始化调用者提供的对齐存储，不调用 `memory.allocator`。`xgl_create()` 通过配置的 `xgl_allocator_t` 一次性申请整个 workspace，返回尚未初始化的 handle，随后须调用 `xgl_init()`。两条路径共用同一分区计划。

准备完成后，初始化、发送、接收处理及清理都只使用 workspace。`xgl_destroy()` 将动态存储一次性归还原 backend；静态存储仍归调用者所有。借用的配置和上下文必须活到销毁结束。

`xgl_memory_report()` 无分配地解释同一份规划，分别报告管理区、初始化区、peer、窗口、可靠记录及载荷、帧 scratch 和乱序 RX 占用。类别字节数包含对齐，合计恰好等于 `report.requirements.size`。失败不改写输出。

```c
xgl_memory_report_t report;
if (xgl_memory_report(&config, &report) == XGL_OK) {
    /* Reserve report.requirements.size at report.requirements.alignment. */
}
```

## 独立资源池

| 资源 | 预留方式 |
| --- | --- |
| 路由和链路 | 固定路由数组；每个独立 PHY 一个 parser cache |
| 路由索引 | embedded-lite/embedded/full 预分配 hash 桶和节点；boot 不包含 |
| Peer 和窗口 | 每个 peer 容量一个 peer 对象及窗口存储 |
| 可靠 TX | 按全局 TX 容量分别预留包记录和 payload 槽 |
| 帧 scratch | 同步且不可重入的 I/O 共用一个完整帧槽 |
| 乱序 RX | 按 RX 容量预留记录和载荷槽，槽大小按帧载荷上限计算 |

每类运行时资源拥有独立固定池，小 payload 不能占用 peer 或控制记录的预留容量。释放的槽位可以复用，不回退 heap，也不在运行期扩容。

## 容量和失败

窗口容量属于单个 peer，TX 和乱序容量属于整个实例。耗尽时返回有界失败或延后推进，不自动扩容。ACK 释放的可靠 TX 槽可供应用提交新的 DATA。

scope 由远端节点、connection ID 和 epoch 标识。恢复时不能在旧 scope 内静默重置序列号。应关闭旧 scope、排空未认证链路旧流量并使用新 epoch；认证模式则关闭可信会话并安装新的可信参数。参见[发送 API](../guides/send.md)。

## 无 Heap 构建

生产构建使用 `-DXGL_ALLOW_FALLBACK_MALLOC=OFF` 禁止 libc 分配。采用静态初始化，或提供带上下文的显式分配器，只申请一次 workspace。启用 NULL fallback 时，也只在 create 边界解析。

协议的无 heap 结论不涵盖应用驱动和认证 provider，应检查包含这些组件的最终固件链接结果。

## 测量

只有 `requirements.size` 表示总字节数。`runtime_blocks` 是所有类别槽数之和；`runtime_block_size` 是最大槽位步长，两者相乘不能描述实际分区布局。

ABI、profile、路由数、帧大小及全部容量都会影响 RAM。用产品工具链测量 workspace 并检查最终链接固件。核算 BSP、应用缓冲、ISR 嵌套和完整调用链所需的栈。
