# Atomic Operations Implementation

## Overview

The `xgl_atomic.h` header provides a portable abstraction layer for atomic operations across different platforms and compilers. This implementation supports:

- **C11 atomic operations** (when available)
- **Windows Interlocked functions**
- **GCC/Clang builtin atomics**
- **Fallback non-atomic implementation** (with warning)

## Supported Operations

### Basic Operations

- `xgl_atomic_init()` - Initialize atomic variable
- `xgl_atomic_load()` - Load value atomically
- `xgl_atomic_store()` - Store value atomically

### Arithmetic Operations

- `xgl_atomic_fetch_inc()` - Atomic increment (returns old value)
- `xgl_atomic_fetch_dec()` - Atomic decrement (returns old value)
- `xgl_atomic_fetch_add()` - Atomic add (returns old value)
- `xgl_atomic_fetch_sub()` - Atomic subtract (returns old value)

### Advanced Operations

- `xgl_atomic_exchange()` - Atomic exchange
- `xgl_atomic_compare_exchange()` - Compare-and-swap (CAS)

### Boolean Operations

- `xgl_atomic_bool_init()` - Initialize atomic boolean
- `xgl_atomic_bool_load()` - Load boolean atomically
- `xgl_atomic_bool_store()` - Store boolean atomically

### Memory Barriers

- `xgl_atomic_fence()` - Full memory barrier
- `xgl_atomic_compiler_barrier()` - Compiler barrier (prevents reordering)

## Convenience Macros

- `xgl_atomic_inc(ptr)` - Increment and return new value
- `xgl_atomic_dec(ptr)` - Decrement and return new value
- `xgl_atomic_add(ptr, val)` - Add and return new value
- `xgl_atomic_sub(ptr, val)` - Subtract and return new value

## Platform Support

### C11 Atomics (Preferred)

When C11 `<stdatomic.h>` is available, the implementation uses standard atomic types and operations:

```c
#include <stdatomic.h>
typedef atomic_uint xgl_atomic_t;
```

### Windows

Uses Windows Interlocked functions:

```c
InterlockedIncrement()
InterlockedDecrement()
InterlockedExchange()
InterlockedCompareExchange()
```

### GCC/Clang

Uses compiler builtin atomics:

```c
__atomic_fetch_add()
__atomic_fetch_sub()
__atomic_load_n()
__atomic_store_n()
```

### Fallback

For platforms without atomic support, provides non-atomic volatile operations with a compiler warning. External synchronization is required in this case.

## Usage Examples

### Reference Counting

```c
typedef struct {
    xgl_atomic_t ref_count;
    /* ... other fields ... */
} xgl_packet_t;

void xgl_packet_acquire(xgl_packet_t* packet) {
    xgl_atomic_fetch_inc(&packet->ref_count);
}

void xgl_packet_release(xgl_packet_t* packet) {
    uint32_t old_count = xgl_atomic_fetch_dec(&packet->ref_count);
    if (old_count == 1) {
        /* Last reference, free the packet */
        xgl_packet_free(packet);
    }
}
```

### Statistics Counters

```c
typedef struct {
    xgl_atomic_t tx_packets;
    xgl_atomic_t rx_packets;
    xgl_atomic_t errors;
} xgl_stats_t;

void xgl_stats_increment_tx(xgl_stats_t* stats) {
    xgl_atomic_fetch_inc(&stats->tx_packets);
}

uint32_t xgl_stats_get_tx(xgl_stats_t* stats) {
    return xgl_atomic_load(&stats->tx_packets);
}
```

### Lock-Free Flag

```c
xgl_atomic_bool_t shutdown_flag;

void xgl_init(void) {
    xgl_atomic_bool_init(&shutdown_flag, false);
}

void xgl_shutdown(void) {
    xgl_atomic_bool_store(&shutdown_flag, true);
}

bool xgl_is_shutdown(void) {
    return xgl_atomic_bool_load(&shutdown_flag);
}
```

### Compare-and-Swap

```c
bool xgl_try_acquire_lock(xgl_atomic_t* lock) {
    uint32_t expected = 0;  /* Unlocked */
    uint32_t desired = 1;   /* Locked */
    return xgl_atomic_compare_exchange(lock, &expected, desired);
}
```

## Thread Safety

Atomic operations are only enabled when `XGL_THREAD_SAFE` is defined. When thread safety is disabled, the atomic types become regular non-atomic types with no overhead.

```c
#ifdef XGL_THREAD_SAFE
    /* Use actual atomic operations */
#else
    /* Use regular operations (no overhead) */
#endif
```

## Memory Ordering

All atomic operations use sequential consistency (strongest memory ordering) to ensure correctness across all platforms. This provides:

- Total order of all atomic operations
- No reordering of atomic operations
- Synchronization between threads

For platforms that support it, this is implemented as:
- C11: `memory_order_seq_cst`
- GCC/Clang: `__ATOMIC_SEQ_CST`
- Windows: Implicit in Interlocked functions

## Testing

Comprehensive unit tests are provided in `test/test_atomic.cpp`:

- Basic operations (init, load, store)
- Arithmetic operations (inc, dec, add, sub)
- Advanced operations (exchange, compare-and-swap)
- Boolean operations
- Edge cases (zero, max value, wraparound)
- Reference counting simulation
- Memory barriers

Run tests with:

```bash
./build/test/xgl_tests --gtest_filter=XglAtomicTest.*
```

## Requirements Validation

This implementation satisfies:

- **Requirement 2.4**: Reference counting uses atomic operations to prevent race conditions
- **Requirement 9.2**: Thread-safe atomic operations for shared state

## Performance Considerations

- **C11/GCC/Clang**: Single instruction on most architectures (e.g., `LOCK ADD` on x86)
- **Windows**: Optimized Interlocked functions
- **Fallback**: No atomic guarantees, requires external synchronization

## Compiler Support

- **GCC 4.7+**: Full support via `__atomic_*` builtins
- **Clang 3.1+**: Full support via `__atomic_*` builtins
- **MSVC 2015+**: Full support via Interlocked functions
- **C11 compliant compilers**: Full support via `<stdatomic.h>`

## Known Limitations

1. **32-bit atomics only**: Currently only supports 32-bit atomic operations
2. **Sequential consistency**: Always uses strongest memory ordering (may be slower on some architectures)
3. **No 64-bit atomics**: 64-bit atomic operations not yet implemented
4. **Fallback warning**: Non-atomic fallback emits compiler warning

## Future Enhancements

- Add 64-bit atomic operations (`xgl_atomic64_t`)
- Add relaxed memory ordering options for performance
- Add atomic pointer operations
- Add atomic bitwise operations (AND, OR, XOR)
