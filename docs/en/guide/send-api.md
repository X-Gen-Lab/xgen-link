# Send API

Use `xgl_send_at(handle, &tx, now_ms)` with the same monotonic millisecond clock used by `xgl_step()`. A successful return means local acceptance, not remote application completion.

## Data Ownership

The payload is borrowed during the call. Reliable sends copy bytes into bounded protocol-owned slots before retaining them for retransmission. The caller may reuse its input after return. PHY TX must consume or copy the frame synchronously and must not retain a protocol pointer.

```c
xgl_tx_data_t tx = {0};
tx.target_id = 2;
tx.data = payload;
tx.data_len = payload_len;
tx.reliable = true;
tx.connection_id = connection_id;
tx.session_epoch = session_epoch;
xgl_error_t result = xgl_send_at(handle, &tx, now_ms);
```

The example assumes valid application variables and an initialized handle. Payload length must be nonzero; priority is 0–7. Keep reserved `compression_id` zero.

## Reliable and Unreliable

Reliable sends are constrained by the peer window and global TX capacity. Continue calling `xgl_step()` to receive ACKs, advance retransmissions, and report terminal failures.

Unreliable sends do not create an ACK/retry guarantee. A PHY success confirms local frame acceptance only. The SDK has no public per-message delivery-completion callback; use an application response if that distinction matters.

## Fragmentation

Fragmentation requires a supporting profile and explicit message/reassembly budgets. A reliable message can span more fragments than the window: one retained message per peer feeds the window as ACKs free packet slots. Temporary PHY/resource backpressure pauses the pump. Do not resubmit a message merely because not all fragments were emitted synchronously.

For reliable fragmented sends, `XGL_OK` means message admission. A later hard failure can be reported through the error callback, including during the initial call. Terminal failure releases retained work and fails that scope.

Unreliable fragmentation sends fragments synchronously. An error can occur after earlier fragments have reached the PHY; retrying the entire application message is not transactional.

## Receive Admission and Callbacks

Prefer `rx_accept_callback`: return `XGL_OK` after consuming or copying the payload, or `XGL_ERR_BUSY` for temporary backpressure. `rx_callback`, if used instead, cannot refuse delivery.

A reliable packet is ACKed only after application acceptance or admission into owned bounded receive storage. A completed fragmented message may therefore already be ACKed while waiting for the application; its bytes remain retained and charged to the RX budget. Keep stepping to retry delivery.

All callback buffers are borrowed only for that call. Callbacks must not reenter, close, or destroy the instance; queue recovery work for after return.

## Failure and Scope Recovery

Treat `XGL_ERR_WINDOW_FULL`, `XGL_ERR_BUSY`, and `XGL_ERR_NO_MEMORY` as capacity/backpressure conditions where appropriate; process incoming work before retrying a rejected submission. Check other errors for invalid input, route failure, PHY failure, or unsupported capability.

A reliable scope is identified by remote ID, connection ID, and epoch. A scope that has used reliable packet numbers is not silently reclaimed by an idle timeout. Hard failure or RESET must not cause sequence-number reuse. Close an unauthenticated scope with `xgl_close_peer()`, drain stale link traffic, and reconnect with a new epoch. Pending TX reports `XGL_ERR_CANCELLED` on explicit close. Authenticated peers must instead close the security session and install fresh trusted parameters.

## Application Data Type

`data_type` is an application classification byte. Nonzero values add DATA_TYPE_EXT and reduce available payload space. It is independent of the protocol's DATA/ACK/control packet type. Include extension, authentication, and CRC overhead when choosing MTU.
