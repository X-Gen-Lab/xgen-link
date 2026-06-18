# Memory Subsystem

The XGL memory subsystem provides predictable, heap-free memory management for the protocol stack. All memory operations go through a layered allocator abstraction, from the low-level allocator interface to the tiered pool and packet pool, each addressing allocation needs at different granularities.

## Design Goals

- **Zero heap dependency**: All memory is pre-allocated in strict no-heap profiles; no malloc/free calls.
- **Predictable latency**: Fixed-block mempools guarantee O(1) allocation and deallocation.
- **Trackable**: A tracking allocator records per-phase allocation statistics for resource budgeting.
- **Portable**: Users can inject custom allocators or use the built-in malloc wrapper.

## Allocation Layer Hierarchy

```text
xgl_alloc() / xgl_free() ← unified entry point
    ↓
xgl_allocator_t (user-provided malloc/free function pointers)
    ↓ (or NULL → controlled by XGL_ALLOW_FALLBACK_MALLOC)
xgl_tracking_allocator_t (5-phase allocation statistics wrapper)
    ↓
xgl_mempool_t (fixed-block-size free list)
    ↓ (or thread-safe: xgl_mempool_ts_t)
xgl_tiered_pool_t (three-tier: ≤64 / ≤256 / ≤1024)
    ↓
xgl_packet_pool_t (packet object pool + reference counting)
```

## Allocator Interface

### Base Allocator

`xgl_allocator_t` is a simple function pointer pair:

```text
xgl_allocator_t
├── alloc_fn  (void* (*)(size_t size, void* user_data))
└── free_fn   (void (*)(void* ptr, void* user_data))
```

- `xgl_alloc(allocator, size)`: Allocate via the allocator; uses the default allocator when NULL.
- `xgl_free(allocator, ptr)`: Free memory.

### Default Allocator

`xgl_allocator_get_default()` returns a malloc/free-based default allocator.

- `XGL_ALLOW_FALLBACK_MALLOC=1` (default): Fallback to malloc is allowed.
- `XGL_ALLOW_FALLBACK_MALLOC=0`: Strict no-heap mode; the default allocator returns NULL.

### Tracking Allocator

`xgl_tracking_allocator_t` wraps an underlying allocator and records allocation statistics per phase.

**5 Phases:**

| Phase | Enum | Description |
| --- | --- | --- |
| INIT | `XGL_ALLOCATOR_PHASE_INIT` | Instance creation and initialization |
| TX | `XGL_ALLOCATOR_PHASE_RUNTIME_TX` | Steady-state transmit path |
| RX | `XGL_ALLOCATOR_PHASE_RUNTIME_RX` | Steady-state receive path |
| RELIABLE | `XGL_ALLOCATOR_PHASE_RELIABLE` | Reliable retransmission storage |
| FRAGMENT | `XGL_ALLOCATOR_PHASE_FRAGMENT` | Fragmentation and reassembly |

**Statistics:**

```text
xgl_allocator_stats_t
├── total_allocated   (size_t — total bytes allocated)
├── total_freed       (size_t — total bytes freed)
├── current_allocated (size_t — currently allocated bytes)
├── peak_allocated    (size_t — peak allocated bytes)
├── alloc_count       (size_t — number of allocations)
└── free_count        (size_t — number of frees)
```

Switch phases with `xgl_tracking_allocator_set_phase()`; statistics are automatically categorized.

## Memory Pool

### Fixed-Block Mempool

`xgl_mempool_t` implements a free-list-based fixed-block-size memory pool.

```text
xgl_mempool_t
├── pool        (uint8_t* — pre-allocated buffer)
├── block_size  (size_t — bytes per block)
├── block_count (size_t — total block count)
├── free_count  (size_t — free block count)
├── peak_used   (size_t — peak blocks in use)
└── free_list   (void* — free list head)
```

**How it works:**

1. At initialization, the pre-allocated buffer is divided into equal-sized blocks.
2. Each free block stores a pointer in its first `sizeof(void*)` bytes, forming a singly-linked list.
3. Allocate: pop from list head — O(1).
4. Free: push to list head — O(1).

### Thread-Safe Mempool

When `XGL_THREAD_SAFE` is enabled, `xgl_mempool_ts_t` wraps a mempool with a mutex.

```text
xgl_mempool_ts_t
├── pool  (xgl_mempool_t — underlying mempool)
└── mutex (xgl_mutex_t — mutual exclusion lock)
```

All operations acquire the lock, delegate to the underlying mempool, then release the lock. Suitable for shared pools in multi-threaded environments.

## Tiered Pool

`xgl_tiered_pool_t` divides memory into three tiers by size, each backed by an independent mempool.

| Tier | Max Block Size | Use Case |
| --- | --- | --- |
| Small | 64 bytes | Headers, metadata, small control structures |
| Medium | 256 bytes | Medium payloads, TLV extensions |
| Large | 1024 bytes | Large payloads, frame buffers |

### Allocation Strategy

1. Request `size` bytes.
2. If `size ≤ 64`: allocate from `small_pool`.
3. Else if `size ≤ 256`: allocate from `medium_pool`.
4. Else if `size ≤ 1024`: allocate from `large_pool`.
5. Otherwise: return NULL.

### Two Initialization Modes

1. **Dynamic** (`xgl_tiered_pool_init`): Internally allocates buffers via the allocator; sets `owns_buffers=true`.
2. **Static** (`xgl_tiered_pool_init_static`): User provides pre-allocated buffers for no-heap scenarios; sets `owns_buffers=false`.

## Packet Pool

`xgl_packet_pool_t` manages a pre-allocated pool of `xgl_packet_t` objects used throughout the protocol stack.

### Reference Counting

`xgl_packet_data_t` manages payload lifecycle through reference counting:

- `xgl_packet_data_ref(data)`: Increment reference count by 1.
- `xgl_packet_data_unref(data, allocator)`: Decrement; free the buffer when the count reaches zero.
- `xgl_packet_data_create(data, len, allocator)`: Create a data copy with reference count 1.

Reference counting allows multiple packets to share the same payload (e.g., reliable retransmission), avoiding unnecessary memory copies.

## No-Heap Profile

Strict no-heap builds require all memory to be pre-allocated during the initialization phase:

1. **TX Pool**: `TX_POOL_SIZE` bytes for the transmit path.
2. **RX Buffer**: `RX_BUFFER_SIZE` bytes for the receive path.
3. **Route Table**: `route_count × sizeof(xgl_route_item_t)` bytes.
4. **Reliable Queue**: `window_size × sizeof(xgl_reliable_packet_t)` bytes per peer.
5. **Fragment Manager**: `max_reassembly_buffers × sizeof(xgl_reassembly_buffer_t)` bytes.

See [Config Presets](../reference/config-presets.md) for specific resource values across 5 presets.

## Evidence

| Rule | Source | Test |
| --- | --- | --- |
| Mempool init/alloc/free | `src/memory/xgl_mempool.c` | `test/test_mempool.cpp` |
| Tiered pool three-tier allocation | `src/memory/xgl_tiered_pool.c` | `test/test_tiered_pool.cpp` |
| Packet pool + refcount | `src/memory/xgl_packet_pool.c` | `test/test_packet_pool.cpp` |
| Tracking allocator 5 phases | `src/memory/xgl_tracking_allocator.c` | `test/test_allocator.cpp` |
| Thread-safe mempool | `src/memory/xgl_mempool_ts.c` | `test/test_mempool.cpp` |
| Default allocator fallback | `src/memory/xgl_allocator.c` | `test/test_allocator.cpp` |