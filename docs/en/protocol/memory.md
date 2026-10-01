# Memory and Capacity

Memory policy is explicit. Protocol code uses `xgm_allocator_t` services with allocation, release and caller context; a null service is not an implicit request for libc. Host applications may opt into the separate xgen-memory libc allocator. Static applications supply an aligned workspace whose exact size is queried before initialization.

## Resource Owners

| Resource class | Owner and lifetime |
| --- | --- |
| Peer, window and optional RTT | Exact scope, retained until explicit close after reliable numbering starts |
| TX record and payload | One accepted reliable packet until ACK, scope failure or close |
| TX fragment extensions | Metadata retained with a reliable fragment |
| TX message | One pending complete-message copy per peer until all fragments enter records |
| RX record and contiguous payload/TLV storage | Accepted out-of-order packet until delivery or scope termination |
| Reassembly record and payload | Incomplete message; completed payload may transfer to peer delivery storage |
| Scratch | One synchronous frame composition/forwarding operation |

`xgl_protocol_memory_t` binds each class to an explicit service. Allocation and release use the same class. Borrowed packet views carry only payload length and a const byte pointer; they do not imply reference-counted ownership. The fragment manager is embedded in transport context.

The acknowledgement bitmap is embedded in each peer's window, with no separate window allocator. The public memory report retains `window_bytes` as zero; its actual storage belongs to `peer_bytes`. Reliable index buckets, compiled window capacity and optional RTT state determine peer size. Use a report matching the current build identity. See [build and test](../getting-started/build-and-test.md#compiled-resource-options) for configuration.

## Explicit Capacity Contract

`max_peers` and `max_tx_packets` must be nonzero. The TX record limit is shared across all peers. With a one-packet window, `max_rx_buffered_packets` may be zero; a larger window needs positive receive buffering capacity. That limit is also shared across peers.

When fragmentation is enabled, `max_message_size`, `max_reassembly_slots`, `max_reassembly_bytes` and `max_tx_message_bytes` must be positive. Message size cannot exceed `UINT32_MAX`, because FRAGMENT_EXT carries a 32-bit total. The reassembly byte budget includes incomplete messages and complete messages retained for application delivery. The TX message byte budget covers all pending whole-message copies.

Each active reassembly record retains the admitted message's reliability. It cannot be inferred from a peer's prior reliable RX history. Mixing reliable and unreliable fragments is rejected before admission, and failed ACK submission does not change retained attributes. Expiration releases only the message or fails its owning scope according to those attributes; see [reliability](reliability.md#failure-and-recovery).

Capacity exhaustion rejects new admission without pretending the bytes were accepted. Already accepted bytes retain an owner until completion or an observable failure. Scratch is reserved separately so occupied DATA storage does not consume ACK/control serialization capacity.

## Static Workspace Layout

Use `xgl_memory_requirements` followed by `xgl_init_static` with public headers matching the ABI, profile and compiled resource identity. The planner checks alignment and arithmetic overflow and rejects insufficient storage. Per-class pools are implemented by xgen-memory; the protocol does not duplicate their free-list implementation.

For MTU `M`, static TX payload slots reserve at most `M - 26` bytes. Each out-of-order RX node owns one contiguous payload-plus-TLV block with the same combined upper bound of `M - 26`. Both borrowed views refer to that one block, which is released once; there is no separate RX extension slot. Scratch reserves one `M`-byte slot under the synchronous, non-reentrant contract. Fragment TX metadata reserves 14 bytes per TX record. Enabled fragmentation reserves whole-message slots for peers and reassembly payload slots for incomplete messages plus peer-held completed messages; byte admission budgets still apply independently. Query the exact workspace rather than summing these payload bounds alone: instance, parser, routes, indexes, pool metadata and alignment also occupy RAM.

Compiled window capacity determines bitmap storage. Runtime `protocol.window_size` determines the working window and must also satisfy the public API limit. With a reduced compiled capacity, larger runtime settings are rejected before initialization; overriding generated macros cannot enlarge compiled objects. Disabling adaptive RTO removes the peer estimator and sampling path, while retaining reliable records, ACKs and backoff.

Verification: `test/unit/transport/test_transport_storage.cpp`, `test/unit/transport/test_transport_fragment_scope.cpp`, `test/unit/transport/test_transport_fixed_rto.cpp`, `test/property/test_memory_properties.cpp` and `test/cmake/test_transport_resource_config.py`. Workspace queries and host tests do not replace complete target ELF, application stack and board budget validation.
