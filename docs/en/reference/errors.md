# Errors

`xgl_error_t` is the protocol error domain. Generic utility statuses belong to `xgs_status_t` in xgen-status; the protocol maps utility failures at module boundaries.

## Handling

- `WINDOW_FULL`, `QUEUE_FULL`, `BUSY`, `NO_MEMORY`: apply backpressure; do not assume ownership was accepted after a failed send.
- `INVALID_FRAME`, `CRC_FAILED`, `INVALID_VERSION`: reject the frame. Authentication failures are reported as invalid frames.
- `ACK_TIMEOUT`: the exact peer scope enters its terminal failed state. Reconnect with a new epoch.
- `CANCELLED`: explicit close or RESET ended pending transmission.
- `UNSUPPORTED`: the selected profile or API path does not provide the capability.

The receive acceptance callback returns `BUSY` before accepting data; transport does not confirm data that it cannot retain or deliver. Completion callbacks may not reenter the instance. See `test/property/test_error_properties.cpp`.
