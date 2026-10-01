# Two-link forwarding

A sender reaches a receiver through one router with two independent PHY links.

This is a deterministic host simulation using wire v3. The nodes, configuration,
routes, PHY descriptors, application contexts, and static workspaces remain at
fixed addresses until `xgl_destroy()` returns. No protocol heap allocation is
needed. `common/sim_link.h` shares a bounded byte-ring PHY; TX copies bytes before
returning and RX preserves partial reads.

Build from the repository root:

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target multi_node
./build/gcc-test/examples/multi_node/multi_node
```

On Windows, the executable has an `.exe` suffix. Every protocol call runs in one
execution context. The loop passes an explicit `uint32_t` millisecond clock to
`xgl_send_at()` and `xgl_step()`; callbacks never reenter the instance.

Each router PHY has its own byte stream and parser. Routes to node 1 and node 3
refer to distinct descriptors. The router forwards DATA and the returning ACK;
the receiver sees the original end-to-end sender identifier. The program checks
that the application receives exactly one payload.

This target is enabled for the full and embedded profiles. Boot deliberately
compiles forwarding out. The example uses unauthenticated traffic; authenticated
forwarding additionally requires explicit endpoint security sessions and an
application-provided authentication implementation. Routers do not need endpoint
keys: only TTL and CRC fields may change in transit.

The fixed workspace arrays are generous host reservations, not a measured MCU
RAM requirement. Initialization calls `xgl_memory_requirements()` and checks the
exact selected-profile requirement before using caller storage. A board port
must measure its own linked image, workspace, driver buffers, and worst-case stack.
