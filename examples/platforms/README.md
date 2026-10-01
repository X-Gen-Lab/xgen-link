# Board integration sketches

These files document the PHY/application boundary. They are not production board
ports and are not linked into the protocol library.

- `bare_metal_port.c` takes a caller-created handle and advances it with
  `board_millis()` and a bounded RX budget. The board supplies its clock and UART
  copy functions.
- `freertos_port.c` uses one owner task for every call on an instance. Other tasks
  and interrupts enqueue commands; they do not concurrently call the handle.
  Define `XGL_PORT_FREERTOS_EXAMPLE` only when FreeRTOS headers are available.
- `windows_mock_port.c` is a bounded host byte buffer. It retains bytes after a
  partial read and rejects overflow instead of overwriting unread data.

Initialize the instance using `xgl_memory_requirements()` and `xgl_init_static()`.
Keep the configuration, routes, PHY descriptors, callback contexts, and aligned
workspace alive at fixed addresses until `xgl_destroy()`. Example configuration
and storage ownership are shown in `../common/sim_link.h`.

TX borrows the protocol frame only until the callback returns. A DMA driver must
copy into its own bounded buffer or complete its use before returning; queueing
the temporary frame pointer is invalid. RX copies currently available bytes into
the requested buffer, updates the produced length, and never waits for a complete
frame. ISRs may advance driver-owned rings and signal the owner loop/task.

Use `xgl_send_at(handle, data, now_ms)`, `xgl_step(handle, now_ms, budget)`, and
`xgl_next_timeout(handle, now_ms, &delay_ms)`. The caller supplies a monotonic
32-bit millisecond clock with normal unsigned wrap. Schedule often enough that
active timer intervals remain within the supported half-range. No global timer,
OS mutex, or thread-safe callback reentry exists in the portable kernel.

Receive callbacks copy or consume borrowed data; they never send, reset, or destroy
the same instance. Queue replies and recovery work for the next owner-loop tick.
Return BUSY from admission when bounded application storage is unavailable.

Hardware validation still needs real UART/DMA cache ownership, interrupt timing,
RTOS stack high-water marks, target linker maps, and power-failure tests. The host
examples do not establish those properties.
