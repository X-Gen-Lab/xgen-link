# Zero-Copy

`xgl_send_zerocopy()` is designed for single-frame, unreliable sends using caller-owned writable buffers.

## Supported Scope

- True zero-copy: single-frame unreliable send.
- Reliable zero-copy: rejected with `XGL_ERR_INVALID_PARAM`; use `xgl_send()` when ACK/retry semantics are required.

## Buffer Requirements

- Buffer must be writable.
- Payload must reserve space for header and required extensions.
- When `auth_required` is true, reserve SECURITY_EXT and authentication trailer space.
- Route MTU must fit the final frame.

## Offset Rules

Unauthenticated single-frame sends usually place payload at `XGL_FRAME_HEADER_SIZE`. Authenticated paths need an additional SECURITY_EXT, so data offset must match the current frame builder requirements. Do not write magic or CRC manually; let the protocol stack fill them.

## Ownership

The caller buffer must remain valid until the PHY TX callback returns. The zero-copy path bypasses transport and network send processing, then updates their TX statistics explicitly after the raw datalink send succeeds.

## Common Misuse

- No header reservation.
- Provider has no tag length while authentication is required.
- Passing reliable data to `xgl_send_zerocopy()` instead of using `xgl_send()`.
