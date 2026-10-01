# Quick start

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
