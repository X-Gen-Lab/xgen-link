# Routing

XGL uses 16-bit node IDs. Local `source_id` must not be `0` or
`XGL_BROADCAST_ID` (`0xFFFF`). Received frames are rejected when the source is
zero, the source is broadcast, or source equals target except for broadcast.
`target_id` may be the local node, a remote node, or `XGL_BROADCAST_ID`.

TODO(xgen-link): confirm broadcast/multicast address policy beyond
`XGL_BROADCAST_ID`.

## Route Table

Each route binds:

- `target_id`
- PHY callbacks
- `max_frame_size`
- `read_freq_hz`
- metric

Forwarding must check route MTU. Frames larger than `max_frame_size` return `XGL_ERR_BUFFER_TOO_SMALL` and are dropped.

## Routing Decision Order

1. Check whether the frame targets the local node.
2. If local, deliver to transport.
3. If remote, check TTL.
4. Lookup route for `target_id`.
5. Check complete frame length against route MTU.
6. Update TTL and recompute header/frame CRC.
7. Call egress PHY.

## Decision Table

| Input condition | Decision | Error/counter behavior | Evidence |
| --- | --- | --- | --- |
| `source_id == 0` | Drop | `XGL_ERR_INVALID_PARAM`, RX drop count | `src/network/xgl_network.c`, `src/network/xgl_network_receive.c` |
| `source_id == XGL_BROADCAST_ID` | Drop | `XGL_ERR_INVALID_PARAM`, RX drop count | `src/network/xgl_network.c` |
| `source_id == target_id` and target is not broadcast | Drop | `XGL_ERR_INVALID_PARAM`, RX drop count | `src/network/xgl_network.c` |
| `target_id == local_id` | Local delivery to transport | Increment network RX stats | `src/network/xgl_network_receive.c` |
| `target_id == XGL_BROADCAST_ID` | Local delivery to transport | Treated as local by `xgl_network_is_local()` | `include/xgl/internal/xgl_network.h` |
| Remote target and route missing | Drop | `XGL_ERR_ROUTE_NOT_FOUND`, RX drop count, error callback | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| Remote target and `ttl <= 1` | Drop | `XGL_ERR_TTL_EXPIRED`, RX drop count, error callback | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| Remote target and frame exceeds route MTU | Drop | `XGL_ERR_BUFFER_TOO_SMALL`, RX drop count | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |
| Remote target, route found, TTL valid, MTU fits | Forward | Decrement TTL, recompute header CRC and frame CRC, call route PHY TX | `src/network/xgl_network_receive.c`, `test/test_network.cpp` |

## TTL

TTL is decremented on every forwarded hop. A remote packet with `ttl <= 1` returns `XGL_ERR_TTL_EXPIRED` and must not be forwarded; forwarded frames therefore leave the node with `ttl >= 1`.

Authenticated frames keep their original end-to-end tag while TTL changes. The forwarding path only updates TTL and CRC fields; authentication remains valid because hop-mutable fields are excluded from the canonical AAD.

## Delivery Path

- Local target: deliver to transport.
- Remote target: lookup route and forward.
- Broadcast/multicast: address ranges are reserved; current reliable delivery is unicast-focused.

## Multi-PHY Notes

Each PHY can use a different `read_freq_hz`. `xgl_run()` polls routes and combines transport/reassembly deadlines into the next wakeup time. Low-power systems should not wake the full protocol stack at a fixed high frequency just because one PHY is slow.
