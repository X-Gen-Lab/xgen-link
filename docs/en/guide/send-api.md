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

## Caller-Owned Frame Storage

`xgl_send_zerocopy_at()` keeps its original request layout and always uses connection ID and epoch zero. Use `xgl_send_zerocopy_scoped_at()` with `xgl_tx_zerocopy_request_t` for an explicit scope. Both entries support only nonempty, unreliable, single-frame payloads; `reliable=true` is rejected and `timeout_ms` is ignored. They do not provide retained-buffer DMA or retransmission ownership.

Call `xgl_send_zerocopy_layout()` before placing the payload. The query ignores `data.buffer`, `data.buffer_size` and `data.data_offset`; it validates the requested metadata, route, complete-frame MTU and installed authentication scope. It returns `payload_offset` and `frame_size`, including DATA_TYPE, nonzero SESSION, SECURITY, the fixed tag and final CRC. It allocates nothing, changes no counters or security sequence, and calls neither the provider nor the PHY. A successful query does not reserve resources; sending validates the current state again.

```c
xgl_tx_zerocopy_request_t request = {0};
request.data.target_id = 2;
request.data.data_type = 9;
request.data.data_len = payload_len;
request.connection_id = connection_id;
request.session_epoch = session_epoch;
xgl_tx_frame_layout_t layout;
xgl_error_t result = xgl_send_zerocopy_layout(handle, &request, &layout);
if (result == XGL_OK && layout.frame_size <= sizeof(frame_buffer)) {
    request.data.buffer = frame_buffer;
    request.data.buffer_size = sizeof(frame_buffer);
    request.data.data_offset = layout.payload_offset;
    memcpy(frame_buffer + layout.payload_offset, payload, payload_len);
    result = xgl_send_zerocopy_scoped_at(handle, &request, now_ms);
}
```

The example assumes a caller-owned array `frame_buffer` and `<string.h>`. If the frame does not fit that array, report capacity failure instead of submitting. Sending leaves the payload at its original address; headers and trailers are writable and may change even when authentication or PHY submission fails. All bytes remain borrowed only until return.

Network TX packets and payload bytes commit only after successful lower-layer submission, consistently for copied, in-place and forwarded frames. These counters do not prove remote delivery.

Boot's private logical network TX workspace is 32 extension bytes, enough for one ACK_RANGE, SESSION and DATA_TYPE together. Larger logical TX extension lists fail before PHY submission. This profile limit does not change Wire v3: complete-frame encoding/decoding still supports the 255-byte header, and other profiles retain the full 231-byte logical extension workspace.

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
