# Routing

A route maps a destination node to a PHY and its full-frame MTU. Routes are protocol configuration, while generic hash/list storage belongs to xgen-containers. The declared route capacity is fixed; exhaustion returns an error instead of silently expanding storage.

## Lookup and Capacity

Instance initialization rejects static destinations that are zero, the local ID, or duplicates. An explicit broadcast destination is allowed. Whole-table loading checks invalid destinations, null PHYs and duplicates before replacing existing entries; rejected configuration leaves the previous routes intact. Unknown destinations return route-not-found. Boot uses bounded linear lookup; other current profiles use a fixed 16-bucket index. Send planning includes full-frame MTU and SESSION, FRAGMENT, SECURITY, tag and CRC overhead.

`metric` is a reserved compatibility field; it does not select an egress or provide multipath ranking. The private table still supports updating existing destinations and reusing removed slots. Borrowed lookup pointers may become invalid after removal, loading or clearing.

## Link Assembly and Lifetime

Borrowed instance configuration must remain valid and immutable until destruction. Workspace planning and link initialization share the same first-PHY occurrence rule, creating one parser and RX cache per unique PHY. Multiple destinations may share a PHY; their `read_freq_hz` initialization hints merge into the shortest polling interval, independently of configuration order. With millisecond scheduling, a nonzero frequency has a minimum interval of 1 ms.

Routes select destination egress; link objects own polling and receive storage. The compatible public configuration still uses one structure without a second configuration copy. Private route mutations do not register a new PHY, rebuild RX caches or change established polling intervals. The public API does not promise runtime link replacement. PHY registration still comes from route configuration; there is no separate receive-only link configuration entry.

Packet identity is separate from egress selection. Private route updates do not reset transport scopes, DATA numbers or security associations, but this does not make migration of accepted packets to a smaller MTU safe. Products requiring dynamic links must first define capacity, shutdown, in-flight packet and rollback contracts.

## Forwarding

Forwarding is compiled out of Boot. An enabled forwarder validates the complete frame, requires a destination route, checks the outgoing full-frame MTU and rejects TTL values that cannot survive another hop. It obtains bounded scratch, decrements TTL, rewrites the header CRC and final CRC, then synchronously transmits the frame.

The end-to-end authentication tag and authenticated payload remain unchanged. TTL and header CRC are canonicalized out of AAD; forwarding does not install local trust or claim that the destination tag was verified. A locally addressed authenticated frame is verified before transport delivery.

The normal datalink-to-network endpoint requires a validated borrowed view and has no raw fallback. A separate private raw receive function retains complete decoding and the instance frame-size check. Forwarding obtains the sole frame address and length from the view without modifying parser storage. Synchronous serialized submission uses the same scratch allocation instead of reserving another complete frame.

Failed lower-layer submission preserves its original error, such as `XGL_ERR_BUSY`, and releases scratch. The relay does not promise queuing or automatic retry; endpoint transport owns reliability. The return value is authoritative for the operation. Error callbacks report selected events and do not cover every error return.

Relays do not refragment or negotiate path MTU. Senders must respect the path bottleneck. Reliable A-to-B-to-C delivery also needs a route at C with destination A and egress through B for the ACK; a route only to neighbor B is insufficient. RX views carry no ingress PHY, and destination lookup is not source authorization.

## Broadcast and Reliability

Broadcast uses node `0xFFFF`. Plain broadcast is available when the authentication policy allows it. The authentication implementation supports unicast associations only. Broadcast must not be treated as a group reliable-delivery or group-authentication protocol. Capacity and feature compatibility between peers are configured by the application; route discovery does not negotiate transport windows or trusted epochs.

Received broadcast is delivered locally without flooding other PHYs; transmission requires an explicit broadcast egress. TTL limits continued forwarding: 0/1 cannot be forwarded, while locally addressed frames may still be delivered. Egress lookup precedes TTL checking, so a frame with both no route and expired TTL returns `XGL_ERR_ROUTE_NOT_FOUND`.

Verification: `test/unit/network/test_network.cpp`, `test/unit/network/test_network_contracts.cpp`, `test/integration/test_network_routes_contracts.cpp` and `test/integration/test_network_multihop.cpp`. See [statistics](../reference/statistics.md) for counter definitions.
