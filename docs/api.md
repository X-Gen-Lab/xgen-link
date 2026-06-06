# API Guide

## Instance Lifecycle

1. Fill `xgl_config_t` using a preset or explicit fields.
2. Provide one or more `xgl_route_item_t` entries with PHY callbacks.
3. Call `xgl_create`.
4. Call `xgl_init`.
5. Call `xgl_send` or `xgl_send_zerocopy`.
6. Call `xgl_run` periodically.
7. Call `xgl_destroy`.

## Send APIs

`xgl_send` is the default API. It supports unreliable send, reliable send, ACK, retransmission, and fragmentation when enabled.

`xgl_send_zerocopy` requires:

- `buffer` is writable.
- `data_offset` is `XGL_FRAME_HEADER_SIZE`.
- `buffer_size` has room for header, payload, and CRC16.
- The current true zero-copy acceptance path is single-frame unreliable send.
- Reliable zero-copy requests keep reliable retransmission semantics and may copy into the retransmission queue.

Compression and encryption flags are reserved for optional codec modules. They are not enabled by the built-in presets and are not applied by the base link path.

## Routing

Each route maps a target node ID to a PHY. Multi-PHY forwarding uses the route table on receive when the frame target is not local or broadcast.

## Codec Boundary

Codecs are registered through the codec module. Compression and encryption are module-level transforms and should remain optional dependencies outside the base link path.
