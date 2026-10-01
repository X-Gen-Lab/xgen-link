# Low-Power Runtime

The application drives protocol work explicitly. There is no internal clock provider, timer thread, or sleep implementation.

## Relative Timeout API

`xgl_step(handle, now_ms, &budget)` polls each due link and advances transport work. `budget.rx_bytes` is a per-link byte limit; zero skips RX polling while still maintaining transport. `receive_timeout_ms` controls incomplete-frame parser timeout, not an API blocking wait.

`xgl_next_timeout(handle, now_ms, &delay_ms)` returns true when a deadline exists. A zero delay means work is due now. False leaves the output unchanged; handle that case separately instead of sleeping an uninitialized value.

## Main-Loop Integration

Conceptual application loop:

```c
const xgl_work_budget_t budget = {128U, 100U};
for (;;) {
    uint32_t now = board_monotonic_ms();
    xgl_error_t result = xgl_step(handle, now, &budget);
    app_handle_step_result(result);
    uint32_t delay;
    bool timed = xgl_next_timeout(handle, board_monotonic_ms(), &delay);
    app_wait_for_rx_or_timeout(timed, timed ? delay : 0U);
}
```

The board/application functions are integration placeholders, not SDK functions. Make event registration and waiting race-safe so an RX notification cannot be lost between polling and sleep. After waking, read time again.

Route polling deadlines remain part of the timeout result. An RX event may require a later scheduled poll if the configured link polling interval has not elapsed.

## RTOS Integration

Run the same loop in the owner task and wait on an event or the relative timeout. ISR work only captures bytes and signals the task. No protocol call may overlap with another call on the same instance.

Keep callback execution bounded. The RX byte budget does not bound user callback duration or all transport maintenance work, so measure the complete step worst case.

## Time and Recovery

Use one monotonic 32-bit millisecond clock, with elapsed intervals below `2^31` milliseconds. Clock wrap is supported; resetting the clock while the instance remains active is not a supported recovery mechanism.

Continue scheduling while packets, message pumping, or application-busy deliveries are pending. Respect backpressure and terminal scope failure; sleeping indefinitely after a failed send does not release retained resources.
