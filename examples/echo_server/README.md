# Deferred echo

Two nodes exchange one reliable message and verify the returned application bytes.

This is a deterministic host simulation using wire v3. The nodes, configuration,
routes, PHY descriptors, application contexts, and static workspaces remain at
fixed addresses until `xgl_destroy()` returns. No protocol heap allocation is
needed. `common/sim_link.h` shares a bounded byte-ring PHY; TX copies bytes before
returning and RX preserves partial reads.

Build from the repository root:

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target echo_server
./build/gcc-test/examples/echo_server/echo_server
```

On Windows, the executable has an `.exe` suffix. Every protocol call runs in one
execution context. The loop passes an explicit `uint32_t` millisecond clock to
`xgl_send_at()` and `xgl_step()`; callbacks never reenter the instance.

The server admission callback copies the borrowed payload into a bounded pending
slot. The next loop tick sends that copy with `xgl_send_at()` after the callback
has returned. A full pending slot returns `XGL_ERR_BUSY` so transport can retry.
The example succeeds only after the client receives the expected echo.

The fixed workspace arrays are generous host reservations, not a measured MCU
RAM requirement. Initialization calls `xgl_memory_requirements()` and checks the
exact selected-profile requirement before using caller storage. A board port
must measure its own linked image, workspace, driver buffers, and worst-case stack.
