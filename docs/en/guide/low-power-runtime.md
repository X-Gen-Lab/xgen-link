# Low-Power Runtime

Protocol work is driven by periodic application calls to `xgl_run()`.

## Deadline API

`xgl_next_deadline_ms(handle)` returns the next protocol deadline. It returns `XGL_NO_DEADLINE_MS` when no route polling, retransmission, or reassembly deadline is pending.

## Bare-Metal Model

```c
while (1) {
    xgl_run(handle, 100);
    uint32_t wait_ms = xgl_next_deadline_ms(handle);
    sleep_until_rx_or_timeout(wait_ms);
}
```

## RTOS Model

Run the protocol from a task. PHY ISRs should only write to a ring buffer or signal a task; they should not run parser, authentication, or transport logic.
