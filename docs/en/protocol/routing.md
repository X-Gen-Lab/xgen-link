# Routing

XGL uses 16-bit node IDs. `0` and reserved addresses are not valid normal local node IDs.

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
6. Update TTL and required header/auth/CRC boundaries.
7. Call egress PHY.

## TTL

TTL is decremented on every forwarded hop. A remote packet with `ttl <= 1` returns `XGL_ERR_TTL_EXPIRED` and must not be forwarded; forwarded frames therefore leave the node with `ttl >= 1`.

## Delivery Path

- Local target: deliver to transport.
- Remote target: lookup route and forward.
- Broadcast/multicast: address ranges are reserved; current reliable delivery is unicast-focused.

## Multi-PHY Notes

Each PHY can use a different `read_freq_hz`. `xgl_run()` polls routes and combines transport/reassembly deadlines into the next wakeup time. Low-power systems should not wake the full protocol stack at a fixed high frequency just because one PHY is slow.
