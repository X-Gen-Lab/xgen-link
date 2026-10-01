# Memory and Capacity

Memory policy is explicit. Protocol code uses `xgm_allocator_t` services with allocation, release and caller context; a null service is not an implicit request for libc. Host applications may opt into the separate xgen-memory libc allocator. Static applications supply an aligned workspace whose exact size is queried before initialization.

## Resource Owners

| Resource class | Owner and lifetime |
| --- | --- |
| Peer and window | Exact scope, retained until explicit close after reliable numbering starts |
| TX record and payload | One accepted reliable packet until ACK, scope failure or close |
| TX fragment extensions | Metadata retained with a reliable fragment |
| TX message | One pending complete-message copy per peer until all fragments enter records |
| RX record, payload and extensions | Accepted out-of-order packet until delivery or scope termination |
| Reassembly record and payload | Incomplete message; completed payload may transfer to peer delivery storage |
| Scratch | One synchronous frame composition/forwarding operation |

`xgl_protocol_memory_t` binds each class to an explicit service. Allocation and release use the same class. Borrowed packet views carry only payload length and a const byte pointer; they do not imply reference-counted ownership. The fragment manager is embedded in transport context.

## Explicit Capacity Contract

`max_peers` and `max_tx_packets` must be nonzero. The TX record limit is shared across all peers. With a one-packet window, `max_rx_buffered_packets` may be zero; a larger window needs positive receive buffering capacity. That limit is also shared across peers.

When fragmentation is enabled, `max_message_size`, `max_reassembly_slots`, `max_reassembly_bytes` and `max_tx_message_bytes` must be positive. Message size cannot exceed `UINT32_MAX`, because FRAGMENT_EXT carries a 32-bit total. The reassembly byte budget includes incomplete messages and complete messages retained for application delivery. The TX message byte budget covers all pending whole-message copies.

Capacity exhaustion rejects new admission without pretending the bytes were accepted. Already accepted bytes retain an owner until completion or an observable failure. Scratch is reserved separately so occupied DATA storage does not consume ACK/control serialization capacity.

## Static Workspace Layout

Use `xgl_memory_requirements` followed by `xgl_init_static` with matching public ABI/profile headers. The planner checks alignment and arithmetic overflow and rejects insufficient storage. Per-class pools are implemented by xgen-memory; the protocol does not duplicate their free-list implementation.

For MTU `M`, static TX/RX payload slots reserve at most `M - 26` bytes, and RX extension slots reserve `min(M - 26, 231)`. Scratch reserves one `M`-byte slot under the synchronous, non-reentrant contract. Fragment TX metadata reserves 14 bytes per TX record. Enabled fragmentation reserves whole-message slots for peers and reassembly payload slots for incomplete messages plus peer-held completed messages; byte admission budgets still apply independently. Query the exact workspace rather than summing these payload bounds alone: instance, parser, routes, indexes, pool metadata and alignment also occupy RAM.
