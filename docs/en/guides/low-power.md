# Low-Power Runtime

The application drives protocol work explicitly. There is no internal clock provider, timer thread, or sleep implementation.

## Relative Timeout API

`xgl_step(handle, now_ms, &budget)` polls each due link and advances transport work. `budget.rx_bytes` is a per-link byte limit; zero skips RX polling while still maintaining parser expiry and transport. `receive_timeout_ms` controls incomplete-frame parser timeout, not an API blocking wait. Zero disables parser expiry. The latest valid step's timeout applies to all incomplete frames from their original start timestamps; changing it does not restart parsing.

Parser deadlines participate in `xgl_next_timeout()` independently of the PHY polling interval. A 10 ms partial-frame timeout still expires at 10 ms when that PHY is polled only once per second. Expiry resets and reports the frame once, including steps with zero RX bytes.

A step returns its first parser timeout, PHY, delivery or transport error while continuing to consume the received byte chunk and maintain transport. Malformed stream frames are discarded and reported through diagnostics; they do not stop parsing later frames. A successful PHY read alone does not imply successful application admission.

ACK/SACK reception validates feedback and records ready work. DATA retries and further fragments run in transport maintenance after RX processing, possibly in that same step. Ready retransmissions and message pumping can yield a zero timeout; temporary PHY or resource backpressure schedules a positive retry delay. Maintenance returns its first hard error and continues other scopes. BUSY or NO_MEMORY while retrying retained work is normal backpressure and does not alone fail the step. Immediate RX admission and ACK-submission errors still propagate; an ACK failure does not revoke accepted bytes or receive history.

Each due link reads its PHY only once per step, also bounded by the compiled `XGL_DATALINK_RX_CHUNK_SIZE`. `AUTO` selects 32 bytes for Boot and 128 for other profiles. The runtime budget is an upper bound, not a request to drain that many bytes. When choosing a smaller chunk, recheck polls per complete frame, driver buffering and parser timeout; see [Datalink](../explanation/datalink.md).

`xgl_next_timeout(handle, now_ms, &delay_ms)` returns true when a deadline exists. A zero delay means work is due now. False leaves the output unchanged; handle that case separately instead of sleeping an uninitialized value.

This compatible entry always includes PHY polling. If the next step has zero `rx_bytes`, use `xgl_next_timeout_for_work(handle, now_ms, &budget, &delay_ms)` to exclude polling work that cannot execute. Transport deadlines remain active, and partial-frame expiry uses the supplied `receive_timeout_ms`. The query does not read a PHY, run transport, change the latest step policy or restart a partial-frame timer. The timeout must be below `2^31`; invalid input returns false and preserves output. No protocol deadline with RX disabled is a valid result; an application event must wake the owner again.

## Main-Loop Integration

Conceptual application loop:

```c
const xgl_work_budget_t budget = {128U, 100U};
for (;;) {
    uint32_t now = board_monotonic_ms();
    xgl_error_t result = xgl_step(handle, now, &budget);
    app_handle_step_result(result);
    uint32_t delay;
    bool timed = xgl_next_timeout_for_work(
        handle, board_monotonic_ms(), &budget, &delay);
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

Application-busy receive data does not create a continuously ready timer. When the application can accept data again, signal the owner task and drive another step. `xgl_next_timeout()` describes protocol deadlines, not application capacity recovery.
