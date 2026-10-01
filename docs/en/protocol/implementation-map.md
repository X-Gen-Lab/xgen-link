# Implementation Map

This map identifies protocol owners. Public applications include `<xgl/xgl.h>`; internal headers are implementation boundaries, not compatibility aliases for removed utilities.

## Source Ownership

| Contract | Main source |
| --- | --- |
| ABI validation, lifecycle, public stepping | `src/api/` |
| Static workspace planning and resource services | `src/api/xgl_workspace.c` |
| Exact peer lifecycle and close | `src/transport/xgl_transport_peer.c` |
| Atomic ACK validation and application | `src/transport/xgl_transport_ack.c` |
| Deadline collection and periodic work | `src/transport/xgl_transport_runtime.c` |
| Production retransmission | `src/transport/xgl_transport_retransmit.c` |
| Owned message pump | `src/transport/xgl_transport_send_fragment.c` |
| Receive ordering and retained delivery | `src/transport/xgl_transport_rx_order.c`, `xgl_transport_delivery.c` |
| Fragment coverage and reassembly | `src/transport/xgl_fragment_*.c` |
| Route lookup and local/forward delivery | `src/network/` |
| Canonical frame layout and TLVs | `src/wire/` |
| Directional session state and authentication | `src/security/` |
| Incremental framing and synchronous PHY | `src/datalink/` |

## Internal Interfaces

`xgl_protocol_io.h` separates packet and frame interfaces using typed arguments. Transport supplies a receive interface and submits logical packets to network. `xgl_packet.h` contains borrowed protocol views. `xgl_protocol_memory.h` describes resource-class services; it does not implement an allocator.

The reliable queue stores records and indexes them. It does not acknowledge scopes independently or retransmit raw PHY frames. Peer-owned transport code performs these state transitions through the current network/security path.

## Verification Boundaries

Unit tests cover ACK validation, scope isolation, cancellation, owned payload lifetime and capacity rejection. Transport properties exercise the production retry path. Static workspace properties use bounded services across multi-window fragmented delivery and receiver backpressure. Wire/security tests assert byte layouts, nonce/AAD construction and replay behavior. SDK/profile smoke builds check the installed public API separately from internal unit fixtures.
