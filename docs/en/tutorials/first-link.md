# Run your first link

## Build and run

From the repository root, build the host Boot example with CMake 3.24 or later, Ninja and a C11 compiler.

```sh
cmake -S . -B build/first-link -G Ninja -DXGL_PROFILE=boot \
  -DXGL_BUILD_EXAMPLES=ON -DXGL_BUILD_NOHEAP_SMOKE=ON
cmake --build build/first-link
ctest --test-dir build/first-link --output-on-failure --no-tests=error -R '^xgl_echo_example$'
```

CTest should run and pass one echo test. On failure, read `build/first-link/Testing/Temporary/LastTest.log`. The example exercises two endpoints over a synchronous simulated PHY.

## Storage and lifetime

Start with `xgl_config_get_preset_boot()` for one peer, one reliable slot and 128-byte MTU. Set local node, routes, PHY and application callbacks. Keep the configuration and referenced objects alive until destruction.

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

Use `xgl_send_at()` outside callbacks. The receive acceptance callback can return BUSY while Flash or application storage is occupied. PHY TX must finish reading or copying the frame before returning.

## Runnable programs

See [examples](examples.md) for complete lifetime-correct programs. The Boot update demonstration models host pacing, fixed blocks, slow Flash and a lost ACK. It is not a secure hardware bootloader.
