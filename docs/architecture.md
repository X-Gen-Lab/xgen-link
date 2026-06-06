# Architecture

xgen-link is layered so embedded ports can replace hardware and scheduler details without changing protocol code.

## Layers

- Application API: `xgl_create`, `xgl_init`, `xgl_send`, `xgl_send_zerocopy`, `xgl_run`, and statistics.
- Transport: reliability, ACK handling, RTT estimate, sliding window, fragmentation and reassembly.
- Network: node addressing, route lookup, broadcast/local delivery, and multi-PHY forwarding.
- Data link: frame serialization, parser, CRC validation, and physical TX/RX calls.
- Platform: time, mutex, atomic, allocator, and physical layer callbacks.

## Data Flow

Transmit path:

```text
application -> transport -> network -> datalink -> PHY tx
```

Receive path:

```text
PHY rx -> datalink parser -> network -> transport -> rx_callback
```

Forwarding path:

```text
PHY rx -> datalink parser -> network route table -> egress PHY tx
```

## Ownership

The standard send API borrows caller data only for the duration of the call. Reliable retransmission stores a copy in the reliable queue. The zero-copy API is intended for caller-owned buffers with reserved header and trailer space; single-frame unreliable TX is framed in the caller buffer and sent through datalink raw TX.

## Embedded Boundaries

Core protocol code avoids hardware headers. Board-specific code lives behind `xgl_phy_ops_t`, platform time/mutex/atomic functions, and the allocator interface. Compression and encryption belong behind the codec module boundary and are not mixed into the base link path.
