# 线程安全

本文档说明 XGL 协议栈在多线程环境下的线程安全机制、锁粒度和使用约束。

## 启用方式

通过 Kconfig 启用:

```text
XGL_THREAD_SAFE=y
```

或编译时定义:

```text
-DXGL_THREAD_SAFE=1
```

启用后,所有 `xgl_mutex_*`、`xgl_mempool_ts_*` 和 `xgl_list_ts_*` 函数从 no-op 切换为真实实现。

## 线程安全层次

```text
┌─────────────────────────────────────────────┐
│ 应用层线程安全                                │
│  xgl_run() 可从单一线程调用                   │
│  xgl_send() 必须从 xgl_run() 同一线程调用     │
├─────────────────────────────────────────────┤
│ 协议栈内部线程安全                             │
│  XGL_THREAD_SAFE 启用时:                      │
│  xgl_mempool_ts_t: 每次 alloc/free 加锁      │
│  xgl_list_ts_t: 每次 insert/remove 加锁       │
│  xgl_mutex_t: 真实平台 mutex                 │
├─────────────────────────────────────────────┤
│ 平台层 mutex 实现                             │
│  POSIX: pthread_mutex_t                      │
│  Windows: CRITICAL_SECTION                   │
│  FreeRTOS: SemaphoreHandle_t                 │
│  Bare-Metal/No-op: 无操作                     │
└─────────────────────────────────────────────┘
```

## 线程安全 API

### Thread-Safe Mempool

| 函数 | 说明 |
| --- | --- |
| `xgl_mempool_ts_init()` | 初始化线程安全内存池 |
| `xgl_mempool_ts_destroy()` | 销毁内存池 |
| `xgl_mempool_ts_alloc()` | 分配块(加锁) |
| `xgl_mempool_ts_free()` | 释放块(加锁) |
| `xgl_mempool_ts_get_free_count()` | 查询空闲数(加锁) |
| `xgl_mempool_ts_get_used_count()` | 查询已用数(加锁) |
| `xgl_mempool_ts_get_peak_used()` | 查询峰值(加锁) |

### Thread-Safe List

| 函数 | 说明 |
| --- | --- |
| `xgl_list_ts_init()` | 初始化线程安全链表 |
| `xgl_list_ts_destroy()` | 销毁链表 |
| `xgl_list_ts_insert_head/tail()` | 插入节点(加锁) |
| `xgl_list_ts_remove()` | 移除节点(加锁) |
| `xgl_list_ts_remove_head/tail()` | 移除头/尾(加锁) |
| `xgl_list_ts_peek_head/tail()` | 查看头/尾(加锁) |
| `xgl_list_ts_is_empty()` | 检查空(加锁) |
| `xgl_list_ts_count()` | 查询数量(加锁) |

### Mutex API

```c
xgl_mutex_t mutex;
xgl_mutex_init(&mutex);

xgl_mutex_lock(&mutex);      // 阻塞获取
xgl_mutex_trylock(&mutex);   // 非阻塞尝试
xgl_mutex_unlock(&mutex);    // 释放

// RAII 风格 (GCC/Clang)
XGL_MUTEX_SCOPED_LOCK(&mutex);

xgl_mutex_destroy(&mutex);
```

## 锁粒度分析

| 组件 | 锁粒度 | 说明 |
| --- | --- | --- |
| `xgl_mempool_ts_t` | 整个池 | 每次 alloc/free 对整个池加锁 |
| `xgl_list_ts_t` | 整个链表 | 每次操作对整个链表加锁 |
| `xgl_mutex_t` | N/A | 本身就是锁原语 |

!!! warning "非粗粒度锁"
    当前实现使用粗粒度锁(整个池/链表级别),不使用 per-element 或 lock-free 算法。这在 MCU 场景下是合理的,因为并发线程数通常 ≤ 4。

## ISR 约束

!!! danger "禁止在 ISR 中执行以下操作"
    - `xgl_run()` — 包含 parser、auth 验证等耗时操作
    - `xgl_send()` — 可能触发内存分配和队列操作
    - 任何 `xgl_mutex_lock()` — ISR 中阻塞获取锁会导致死锁
    - Auth provider 回调 — 可能涉及加密运算

**ISR 安全操作:**
- 仅通过 `xgl_phy_ops_t.rx()` 接口接收字节到环形缓冲区
- 设置标志位通知主循环处理

## 使用模式

### 单线程模式(默认)

```text
// XGL_THREAD_SAFE=n
// 所有 mutex 为 no-op,零开销
void main_loop(void) {
    xgl_run(instance, get_time_ms());
    // send/receive 在同一线程
}
```

### 双线程模式

```text
// XGL_THREAD_SAFE=y
// 线程 A: 协议栈主循环
void protocol_thread(void) {
    while (1) {
        xgl_run(instance, get_time_ms());
    }
}

// 线程 B: 应用逻辑(通过 xgl_send 发送)
void application_thread(void) {
    while (1) {
        xgl_send(instance, &tx_data);
    }
}
```

!!! note "xgl_send 线程安全"
    `xgl_send()` 在 `XGL_THREAD_SAFE` 模式下内部对可靠队列和滑动窗口操作加锁,可以安全地从非 `xgl_run()` 线程调用。但不建议频繁跨线程调用,因为锁竞争会影响性能。

### 多实例隔离

多个 `xgl_instance_t` 互相独立,各自持有独立的层上下文和资源。不同线程可以安全地操作不同实例,无需额外同步。

## Error Callback 线程安全

- `xgl_error_callback_t` 在 `xgl_run()` 上下文中调用
- `XGL_THREAD_SAFE` 模式下,可能从任意 `xgl_run()` 线程调用
- 回调函数内不应执行长时间阻塞操作
- 回调函数内不应调用 `xgl_send()` (可能死锁)

## 性能影响

| 操作 | 无锁 (NOOP) | 有锁 (POSIX) | 有锁 (FreeRTOS) |
| --- | --- | --- | --- |
| mempool alloc | ~5ns | ~50ns | ~200ns |
| list insert | ~3ns | ~40ns | ~150ns |
| mutex lock/unlock | ~0ns | ~25ns | ~100ns |

锁开销在 MCU 主频(48-160MHz)下可接受,但高频 `xgl_send()` 调用需评估锁竞争。

## 证据

| 规则 | 源码 | 测试 |
| --- | --- | --- |
| Thread-safe mempool | `src/memory/xgl_mempool_ts.c` | `test/test_mempool.cpp` |
| Thread-safe list | `src/core/xgl_list_ts.c` | `test/test_list.cpp` |
| Mutex implementation | `src/platform/xgl_mutex.c` | `test/test_mutex.cpp` |
| Mutex noop | `src/platform/xgl_mutex_noop.c` | `test/test_mutex.cpp` |
| Windows mutex | `src/platform/xgl_mutex_windows.c` | `test/test_mutex.cpp` |