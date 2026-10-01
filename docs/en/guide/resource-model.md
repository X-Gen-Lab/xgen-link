# Resource Model

Every instance owns one bounded workspace. The protocol describes resource ownership and capacities; reusable allocation algorithms belong to the independent xgen-memory library.

## Allocation Phases

`xgl_memory_requirements()` validates the configuration and returns exact `size` and `alignment`. The layout includes the instance, pool descriptors, initialization objects, and all runtime slots.

`xgl_init_static()` prepares and initializes caller-owned aligned storage without calling `memory.allocator`. `xgl_create()` uses the configured `xgm_allocator_t` once for the entire workspace, then returns an uninitialized handle; call `xgl_init()` next. Both paths use the same partition plan.

After preparation, init, sends, receive processing, and cleanup use only the workspace. `xgl_destroy()` returns dynamic storage to the original backend once; static storage remains owned by the caller. Borrowed configuration and contexts must outlive destruction.

## Independent Resource Pools

| Resource | Reservation |
| --- | --- |
| Routes and links | Fixed route arrays; one parser cache per distinct PHY |
| Route index | Preallocated hash buckets/nodes in embedded/full; absent in boot |
| Peer and window | One peer object and window storage per peer capacity |
| Reliable TX | Separate packet and payload slots per global TX capacity |
| Frame scratch | One complete-frame scratch slot for synchronous, non-reentrant I/O |
| Out-of-order RX | Separate node, payload, and extension slots per RX capacity |
| Fragmented TX | One maximum-message slot per peer; extension slots per TX capacity |
| Reassembly | One descriptor per reassembly slot |
| Retained RX messages | Maximum-message slots for reassembly slots plus peers |

Each category has its own `xgm_pool_t` service. A small payload cannot consume a peer or control-record reservation. Freed slots are reusable with no heap fallback or runtime expansion.

Fragment byte budgets are shared admission limits, not variable-size arenas. The fixed reservations include completed RX messages retained while the application is busy. These conservative reservations may exceed the configured concurrent byte budget.

## Capacity and Failure

Window capacity is per peer; TX and out-of-order capacity are global. Exhaustion yields a bounded failure or deferred work, not an automatic resize. Reliable fragmented messages can exceed the packet window: retained message bytes feed later packets as ACKs release slots.

A scope is identified by remote node, connection ID, and epoch. Recovery must not silently restart sequence numbers within an old scope. Close the old scope, drain old unauthenticated traffic, and use a new epoch; authenticated recovery closes the trusted session and installs fresh trusted parameters. See [Send API](send-api.md).

## No-Heap Builds

Select `-DXGL_ALLOW_FALLBACK_MALLOC=OFF` and `-DXGM_BUILD_LIBC_ALLOCATOR=OFF` for production without libc allocation. Use static initialization, or provide an explicit context allocator for the single workspace reservation. NULL fallback, when enabled, is resolved only at the create boundary.

A protocol no-heap claim does not cover application drivers or authentication providers. Inspect the final linked firmware, including those components.

## Measurement

Only `requirements.size` is the total byte count. `runtime_blocks` counts slots across all categories; `runtime_block_size` is the largest stride. Multiplying them does not describe the partitioned layout.

ABI, profile, route count, frame size, and all capacities affect RAM. The Cortex-M0 probe in `tools/boot_footprint` generates target layout constants and links real API paths, with map, stack, and heap-symbol reports. Account separately for BSP memory, application buffers, ISR nesting, and total call-stack depth.
