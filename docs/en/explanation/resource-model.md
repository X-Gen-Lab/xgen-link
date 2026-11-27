# Resource Model

Every instance owns one bounded workspace. Private fixed pools manage the protocol's independently bounded resource classes.

## Allocation Phases

`xgl_memory_requirements()` validates the configuration and returns exact `size` and `alignment`. The layout includes the instance, pool descriptors, initialization objects, and all runtime slots.

`xgl_memory_report()` explains the same plan without allocating: management, initialization, peers, windows, reliable records and payloads, frame scratch and buffered RX. Category bytes include alignment and sum exactly to `report.requirements.size`. Failure leaves the report unchanged. Its inline entry checks the consumer's configuration ABI and compiled profile identity.

```c
xgl_memory_report_t report;
if (xgl_memory_report(&config, &report) == XGL_OK) {
    /* Reserve report.requirements.size at report.requirements.alignment. */
}
```

`xgl_init_static()` prepares and initializes caller-owned aligned storage without calling `memory.allocator`. `xgl_create()` uses the configured `xgl_allocator_t` once for the entire workspace, then returns an uninitialized handle; call `xgl_init()` next. Both paths use the same partition plan.

After preparation, init, sends, receive processing, and cleanup use only the workspace. `xgl_destroy()` returns dynamic storage to the original backend once; static storage remains owned by the caller. Borrowed configuration and contexts must outlive destruction.

## Independent Resource Pools

| Resource | Reservation |
| --- | --- |
| Routes and links | Fixed route arrays; one parser cache per distinct PHY |
| Route index | Preallocated hash buckets/nodes in embedded-lite/embedded/full; absent in boot |
| Peer and window | One peer object and window storage per peer capacity |
| Reliable TX | Separate packet and payload slots per global TX capacity |
| Frame scratch | One complete-frame scratch slot for synchronous, non-reentrant I/O |
| Out-of-order RX | A record with decoded scope and packet metadata, plus a fixed payload slot per RX capacity |

Out-of-order payload slots use the configured frame payload upper bound. TLVs are validated during frame decoding. The retained packet keeps only the decoded metadata needed for later delivery.

Each category has its own fixed pool. A small payload cannot consume a peer or control-record reservation. Freed slots are reusable with no heap fallback or runtime expansion.

## Capacity and Failure

Window capacity is per peer; TX and out-of-order capacity are global. Exhaustion yields a bounded failure or deferred work, not an automatic resize.

A scope is identified by remote node, connection ID, and epoch. Recovery must not silently restart sequence numbers within an old scope. Close the old scope, drain old unauthenticated traffic, and use a new epoch; authenticated recovery closes the trusted session and installs fresh trusted parameters. See [Send API](../guides/send.md).

## No-Heap Builds

Select `-DXGL_ALLOW_FALLBACK_MALLOC=OFF` for production without libc allocation. Use static initialization, or provide an explicit context allocator for the single workspace reservation. NULL fallback, when enabled, is resolved only at the create boundary.

A protocol no-heap claim does not cover application drivers or authentication providers. Inspect the final linked firmware, including those components.

## Measurement

Only `requirements.size` is the total byte count. `runtime_blocks` counts slots across all categories; `runtime_block_size` is the largest stride. Multiplying them does not describe the partitioned layout.

ABI, profile, route count, frame size, and all capacities affect RAM. Use the product toolchain to measure workspace size and inspect the linked firmware. Account for BSP memory, application buffers, ISR nesting and total call-stack depth.
