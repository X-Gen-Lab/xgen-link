# Routing

A route maps a destination node to a PHY and its full-frame MTU. Routes are protocol configuration, while generic hash/list storage belongs to xgen-containers. The declared route capacity is fixed; exhaustion returns an error instead of silently expanding storage.

## Lookup and Capacity

Existing route entries may be updated and removed slots reused. Unknown destinations return route-not-found. Boot uses bounded linear lookup; Embedded and Full may use the compiled route index. Link MTU participates in send planning, including SESSION, FRAGMENT, SECURITY, tag and CRC overhead.

Packet identity is unrelated to the selected route. Changing a PHY or MTU does not reset the exact transport scope, DATA numbers or security association. Applications must avoid changing a path to an MTU that cannot carry its already accepted packets.

## Forwarding

Forwarding is compiled out of Boot. An enabled forwarder validates the complete frame, requires a destination route, checks the outgoing full-frame MTU and rejects TTL values that cannot survive another hop. It obtains bounded scratch, decrements TTL, rewrites the header CRC and final CRC, then synchronously transmits the frame.

The end-to-end authentication tag and authenticated payload remain unchanged. TTL and header CRC are canonicalized out of AAD; forwarding does not install local trust or claim that the destination tag was verified. A locally addressed authenticated frame is verified before transport delivery.

## Broadcast and Reliability

Broadcast uses node `0xFFFF`. Plain broadcast is available when the authentication policy allows it. The authentication implementation supports unicast associations only. Broadcast must not be treated as a group reliable-delivery or group-authentication protocol. Capacity and feature compatibility between peers are configured by the application; route discovery does not negotiate transport windows or trusted epochs.

Verification: `test/unit/network/test_network.cpp`.
