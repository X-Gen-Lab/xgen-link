# Fixed-block file transfer

Transfer a 1024-byte in-memory file in sixteen reliable 64-byte application blocks.

This is a deterministic host simulation using wire v3. The nodes, configuration,
routes, PHY descriptors, application contexts, and static workspaces remain at
fixed addresses until `xgl_destroy()` returns. No protocol heap allocation is
needed. `common/sim_link.h` shares a bounded byte-ring PHY; TX copies bytes before
returning and RX preserves partial reads.

Build from the repository root:

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target file_transfer
./build/gcc-test/examples/file_transfer/file_transfer
```

On Windows, the executable has an `.exe` suffix. Every protocol call runs in one
execution context. The loop passes an explicit `uint32_t` millisecond clock to
`xgl_send_at()` and `xgl_step()`; callbacks never reenter the instance.

The sender submits the next block when its one-packet transport window is free.
`XGL_OK` from send means the protocol owns a retransmission copy and accepted the
submission; it does not prove peer receipt. The receiver copies each accepted
ordered block into bounded storage. The program checks all received bytes and
prints the core CRC16/MODBUS result. CRC is an accidental-corruption check, not
an authentication mechanism.

This example intentionally uses application block boundaries and does not need
protocol fragmentation. See `../boot_update` for delayed Flash acceptance,
application commit notifications, host throttling, and dropped-ACK recovery.

The fixed workspace arrays are generous host reservations, not a measured MCU
RAM requirement. Initialization calls `xgl_memory_requirements()` and checks the
exact selected-profile requirement before using caller storage. A board port
must measure its own linked image, workspace, driver buffers, and worst-case stack.
