# Thread Safety

This document describes XGL's thread safety mechanisms, lock granularity, and usage constraints in multi-threaded environments.

## Enabling

Via Kconfig:

```text
XGL_THREAD_SAFE=y
```

Or compile-time definition:

```text
-DXGL_THREAD_SAFE=1
```

When enabled, all `xgl_mutex_*`, `xgl_mempool_ts_*`, and `xgl_list_ts_*` functions switch from no-op to real implementations.

## Thread Safety Layers

```text
┌─────────────────────────────────────────────┐
│ Application-level Thread Safety              │
│  xgl_run() called from a single thread       │
│  xgl_send() must be called from xgl_run()'s  │
│  thread or is internally locked              │
├─────────────────────────────────────────────┤
│ Internal Protocol Stack Thread Safety        │
│  When XGL_THREAD_SAFE enabled:               │
│  xgl_mempool_ts_t: lock per alloc/free       │
│  xgl_list_ts_t: lock per insert/remove       │
│  xgl_mutex_t: real platform mutex            │
├─────────────────────────────────────────────┤
│ Platform Mutex Implementation                │
│  POSIX: pthread_mutex_t                      │
│  Windows: CRITICAL_SECTION                   │
│  FreeRTOS: SemaphoreHandle_t                 │
│  Bare-Metal/No-op: no operation              │
└─────────────────────────────────────────────┘
```

## Thread-Safe API

### Thread-Safe Mempool

All operations acquire the internal mutex before delegating to the underlying mempool.

### Thread-Safe List

All operations acquire the internal mutex before delegating to the underlying list.

### Mutex

```c
xgl_mutex_t mutex;
xgl_mutex_init(&mutex);

xgl_mutex_lock(&mutex);      // blocking acquire
xgl_mutex_trylock(&mutex);   // non-blocking try
xgl_mutex_unlock(&mutex);    // release

// RAII-style (GCC/Clang)
XGL_MUTEX_SCOPED_LOCK(&mutex);

xgl_mutex_destroy(&mutex);
```

## Lock Granularity

| Component | Granularity | Notes |
| --- | --- | --- |
| `xgl_mempool_ts_t` | Per-pool | Lock for every alloc/free on the whole pool |
| `xgl_list_ts_t` | Per-list | Lock for every operation on the whole list |

!!! warning "Coarse-grained locks"
    Current implementation uses coarse-grained locks (per-pool/per-list), not per-element or lock-free algorithms. This is appropriate for MCU scenarios with typically ≤ 4 concurrent threads.

## ISR Constraints

!!! danger "Never execute the following from ISR context"
    - `xgl_run()` — contains parser, auth verification, and other expensive operations
    - `xgl_send()` — may trigger memory allocation and queue operations
    - Any `xgl_mutex_lock()` — blocking lock acquisition in ISR causes deadlock
    - Auth provider callbacks — may involve cryptographic computation

**ISR-safe operations:**

- Receiving bytes into a ring buffer via `xgl_phy_ops_t.rx()`
- Setting flags to notify the main loop

## Usage Patterns

### Single-Thread (Default)

```text
// XGL_THREAD_SAFE=n
// All mutexes are no-op, zero overhead
void main_loop(void) {
    xgl_run(instance, get_time_ms());
}
```

### Dual-Thread

```text
// XGL_THREAD_SAFE=y
// Thread A: protocol stack main loop
void protocol_thread(void) {
    while (1) xgl_run(instance, get_time_ms());
}

// Thread B: application logic (sends via xgl_send)
void application_thread(void) {
    while (1) xgl_send(instance, &tx_data);
}
```

### Multi-Instance Isolation

Multiple `xgl_instance_t` instances are fully independent with their own layer contexts and resources. Different threads can safely operate on different instances without additional synchronization.

## Error Callback Thread Safety

- `xgl_error_callback_t` is called within the `xgl_run()` context.
- In `XGL_THREAD_SAFE` mode, it may be called from any `xgl_run()` thread.
- The callback must not perform long blocking operations.
- The callback must not call `xgl_send()` (potential deadlock).

## Performance Impact

| Operation | No-op (NOOP) | POSIX | FreeRTOS |
| --- | --- | --- | --- |
| mempool alloc | ~5ns | ~50ns | ~200ns |
| list insert | ~3ns | ~40ns | ~150ns |
| mutex lock/unlock | ~0ns | ~25ns | ~100ns |

Lock overhead is acceptable at MCU clock speeds (48-160MHz), but high-frequency `xgl_send()` calls should evaluate lock contention.

## Evidence

| Rule | Source | Test |
| --- | --- | --- |
| Thread-safe mempool | `src/memory/xgl_mempool_ts.c` | `test/test_mempool.cpp` |
| Thread-safe list | `src/core/xgl_list_ts.c` | `test/test_list.cpp` |
| Mutex implementation | `src/platform/xgl_mutex.c` | `test/test_mutex.cpp` |
| Mutex noop | `src/platform/xgl_mutex_noop.c` | `test/test_mutex.cpp` |
| Windows mutex | `src/platform/xgl_mutex_windows.c` | `test/test_mutex.cpp` |