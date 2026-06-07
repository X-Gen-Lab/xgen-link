# Zero-Copy

`xgl_send_zerocopy()` is designed for single-frame, unreliable sends using caller-owned writable buffers.

## Supported Scope

- True zero-copy: single-frame unreliable send.
- Reliable zero-copy: may copy into the reliable queue to preserve retransmission semantics.

## Buffer Requirements

- Buffer must be writable.
- Payload must reserve space for header and required extensions.
- When `auth_required` is true, reserve SECURITY_EXT and authentication trailer space.
- Route MTU must fit the final frame.

## Offset Rules

Unauthenticated single-frame sends usually place payload at `XGL_FRAME_HEADER_SIZE`. Authenticated paths need an additional SECURITY_EXT, so data offset must match the current frame builder requirements. Do not write magic or CRC manually; let the protocol stack fill them.

## Ownership

The caller buffer must remain valid until the PHY TX callback returns. Reliable zero-copy may copy data for retransmission, so applications must not infer reliability state from whether a copy happened.

## Common Misuse

- No header reservation.
- Provider has no tag length while authentication is required.
- Treating reliable zero-copy as guaranteed no-copy.
