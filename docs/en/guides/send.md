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

The example assumes valid application variables and an initialized handle. Payload length must be nonzero; `priority` is 0–7 and is encoded only in the wire traffic class. The SDK does not use it to order local sends or preempt other peers. Keep reserved `compression_id` zero.

## Caller-Owned Frame Storage

`xgl_send_zerocopy_at()` keeps its original request layout and always uses connection ID and epoch zero. Use `xgl_send_zerocopy_scoped_at()` with `xgl_tx_zerocopy_request_t` for an explicit scope. Both entries support only nonempty, unreliable, single-frame payloads; `reliable=true` is rejected and `timeout_ms` is ignored. They do not provide retained-buffer DMA or retransmission ownership.

Call `xgl_send_zerocopy_layout()` before placing the payload. The query ignores `data.buffer`, `data.buffer_size` and `data.data_offset`; it validates the requested metadata, route, complete-frame MTU, installed authentication scope and terminal-scope policy. It returns `payload_offset` and `frame_size`, including DATA_TYPE, nonzero SESSION, SECURITY, the fixed tag and final CRC. It allocates nothing, changes no counters or security sequence, and calls neither the provider nor the PHY. Failure clears the output. A successful query does not reserve resources; sending validates the current state again.

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

For local sends, Network composes Transport-generated extensions using 32 bytes of temporary storage. Encoding adds SECURITY when required. Receive and forwarding validate complete Wire frames, whose header limit is 255 bytes.

## Reliable and Unreliable

Reliable sends are constrained by the peer window and global TX capacity. Continue calling `xgl_step()` to receive ACKs, advance retransmissions, and report terminal failures.

For ordinary sends, `timeout_ms` sets the initial ACK wait for each reliable packet, so the application still owns any end-to-end operation deadline. Zero uses the RTT estimate supported by the current profile, falling back to the configured ACK wait when no estimate exists, or the built-in default if that configuration is also zero; retransmission may adjust the wait. Ordinary unreliable sends create no ACK timer. Both zero-copy send entries and the layout query ignore this field.

`reliable=true` requires a unicast target. A broadcast target (`0xFFFF`) is rejected with `XGL_ERR_INVALID_PARAM` before allocating peer or retransmission state. Plain unreliable broadcast remains available through an explicit route when authentication policy permits it.

Unreliable sends do not create an ACK/retry guarantee. A PHY success confirms local frame acceptance only. The SDK has no public per-message delivery-completion callback; use an application response if that distinction matters.

## Application Blocks

Each DATA submission fits in one frame. The payload budget is the effective frame MTU minus the base header, TLVs, authentication tag when present, and final CRC. Check the [wire budget](../reference/protocol/v1/wire-format.md#size-budget) and both directions of the route for reliable traffic. A payload above the single-frame budget returns `XGL_ERR_BUFFER_TOO_SMALL` before creating a peer, sending HELLO or submitting DATA.

For a larger transfer, choose a block size that fits the end-to-end path, including any application metadata. Send blocks independently with `xgl_send_at()` and use application IDs or offsets to correlate them. The [file transfer example](../tutorials/examples.md) sends fixed blocks. Firmware updates can use application receipts to confirm durable writes.

## Receive Admission and Callbacks

Prefer `rx_accept_callback`: return `XGL_OK` after consuming or copying the payload, or `XGL_ERR_BUSY` for temporary backpressure. `rx_callback`, if used instead, cannot refuse delivery.

Local DATA requires a receive callback. The Advanced scoped callback takes precedence, followed by the accept and legacy callbacks. With all three absent, `xgl_step*()` returns `XGL_ERR_UNSUPPORTED` for local DATA before retention or acknowledgement. A send-only endpoint can leave these callbacks empty and still process ACK and CONTROL. To accept and discard DATA, install an explicit acceptance callback.

An in-order reliable packet is ACKed after application acceptance. An out-of-order packet can be SACKed once the protocol has retained its bytes. Keep stepping after application capacity returns to deliver retained packets.

An ACK/SACK submission error is returned without undoing accepted bytes or receive numbering. Retrying the same incoming DATA never repeats successful application delivery. During later maintenance, BUSY or NO_MEMORY retains owned data as normal backpressure; a hard application error fails the owning scope, releases its bytes and is reported once. Notify the protocol owner when application capacity returns, since pending application delivery alone does not create a ready timeout.

All callback buffers are borrowed only for that call. Callbacks must not reenter, close, or destroy the instance; queue recovery work for after return.

The existing `rx_callback` and `rx_accept_callback` expose only the sender ID, application data type and payload, without connection ID, epoch or packet number. `error_callback` exposes only the instance, error code and diagnostic text, without the failed scope or a per-message identifier; the text can also be empty. The SDK has no per-message send-completion callback. These callbacks alone cannot distinguish multiple scopes for the same remote or confirm remote processing of a particular message. Carry correlation identifiers and define responses in the application protocol when required.

## Failure and Scope Recovery

Treat `XGL_ERR_WINDOW_FULL`, `XGL_ERR_BUSY`, and `XGL_ERR_NO_MEMORY` as capacity/backpressure conditions where appropriate; process incoming work before retrying a rejected submission. Check other errors for invalid input, route failure, PHY failure, or unsupported capability.

`xgl_step()` reports the first receive or hard maintenance error while continuing independent work. A maintenance retry that retains data after temporary backpressure does not require the application to submit that data again. Error callbacks describe scope failures and selected diagnostics; they are not a replacement for checking operation return values.

A reliable scope is identified by remote ID, connection ID, and epoch. Once an existing scope becomes terminal, all DATA TX in that exact scope is rejected, including ordinary reliable or unreliable sends and both zero-copy send entries. The pure layout query is also rejected; valid requests return `XGL_ERR_ACK_TIMEOUT`. Other connection IDs or epochs are unaffected by that terminal state. Rejection creates no peer, calls neither the authentication provider nor the PHY, and adds no transmission-attempt statistics.

A scope that has used reliable packet numbers is not silently reclaimed by an idle timeout. Hard failure or RESET must not cause sequence-number reuse. Close an unauthenticated scope with `xgl_close_peer()`, drain stale link traffic, and reconnect with a new epoch. Pending TX reports `XGL_ERR_CANCELLED` on explicit close. Authenticated peers must instead close the security session and install fresh trusted parameters.

## Application Data Type

`data_type` is an application classification byte. Nonzero values add DATA_TYPE_EXT and reduce available payload space. It is independent of the protocol's DATA/ACK/control packet type. Include extension, authentication, and CRC overhead when choosing MTU.
