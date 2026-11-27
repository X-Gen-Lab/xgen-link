# Run your first link

## Build and run

From the repository root, build the two-endpoint echo example with CMake 3.24 or later, Ninja and a C11 compiler.

```sh
cmake -S . -B build/first-link -G Ninja -DXGL_PROFILE=embedded-lite \
  -DXGL_BUILD_EXAMPLES=ON
cmake --build build/first-link --target echo_server
./build/first-link/examples/echo_server/echo_server
```

The program should print `Deferred echo: PASS`. It exercises two endpoints over a synchronous simulated PHY. The executable has an `.exe` suffix on Windows.

## Storage and lifetime

The example starts with the Tiny preset and sets the reliable window and peer count to one with a 128-byte MTU. Configure each node's ID, route to the other endpoint, PHY and application callback. Keep the configuration and referenced objects alive until destruction.

Call `xgl_memory_requirements()`, check the exact size and alignment, and supply that capacity to `xgl_init_static()`. Different profiles and limits require different sizes. Alternatively, `xgl_create()` reserves the same layout through the selected backend and requires `xgl_init()`.

## Main loop

```c
const xgl_work_budget_t budget = {128U, 1000U};
(void)xgl_step(handle, now_ms, &budget);
uint32_t delay_ms;
if (xgl_next_timeout(handle, now_ms, &delay_ms)) {
    /* Application chooses the next wakeup. */
}
```

Use `xgl_send_at()` outside callbacks. The receive acceptance callback can return BUSY while application storage is occupied. PHY TX finishes reading or copying the complete frame before returning. Handle `xgl_step()` receive and maintenance errors as described in the [send API](../guides/send.md).

## Runnable programs

See [examples](examples.md) for complete programs, including reliable application chunks and multihop delivery.
