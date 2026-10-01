# 移植

协议使用 C11，消费五个独立基础包以及调用者提供的 I/O 和时间，不内置 OS、硬件时钟、mutex 或调度器 backend。依赖入口见[模块化迁移](modular-migration.md)。

## PHY 契约

```c
xgl_error_t tx(const uint8_t *data, size_t len, void *user_data);
xgl_error_t rx(uint8_t *buffer, size_t *len, void *user_data);
```

TX 收到一帧完整数据，只有消费或复制全部字节后才能返回 `XGL_OK`。包括 DMA 在内，返回后不得保留该指针。临时背压只能表示整帧未接纳，接口不能表达半帧接纳。

RX 的 `*len` 输入为可写容量，输出为实际字节数，绝不能超出容量。无数据时返回 `XGL_OK` 并将长度置零。调用须非阻塞或具有明确执行时间上限；回调不能重入实例。

## 时间和调度

应用为发送、step 和超时查询提供 `uint32_t now_ms`。实例生命周期内统一使用同一单调毫秒时间源。支持无符号回绕；时间间隔和经过时间的比较必须小于 `2^31` 毫秒。

定期或在 RX/定时事件到达时调用 `xgl_step()`。`xgl_next_timeout()` 返回下次唤醒的相对延迟。不需要注册全局时间函数，也不需要协议定时器线程。

## 存储和生命周期

优先使用对齐的调用者存储与 `xgl_init_static()`。先查询目标 ABI 的精确需求，不能将宿主机结构体大小直接复制到固件。动态创建接受显式 `xgm_allocator_t`：回调接收其 `ctx`，返回满足对齐要求的存储，并在销毁时释放唯一 workspace。

Workspace、不可变配置、PHY 描述符、provider 描述符及上下文必须保持地址固定且活到销毁。不同实例需要不同 workspace，以及独立管理的上下文。

## ISR 和 RTOS 集成

ISR 可以写应用自有 ring 或通知协议任务。解析、认证、ACK 处理及用户回调在串行化的任务或主循环上下文中执行。

每个实例使用单一 owner task，或由应用锁覆盖所有 API，包括 stats 和 destroy。递归 mutex 不能让回调重入变安全。异步驱动对数据的所有权必须在 PHY TX 返回前由适配层处理完整。

## 验证

使用真实驱动验证拆分帧、突发数据、RX ring 满、PHY 背压、ACK 丢失、时钟回绕及资源耗尽。Cortex-M0 footprint 消费者可作为链接测量参考，其启动代码和空 RX 回调不是 BSP。
