# Datalink

Datalink separates PHY byte I/O from protocol packets. It does not own reliability, peer identity or trusted sessions.

## Receive Framing

PHY receive bytes may end in the middle of a header or frame, or contain several frames. The incremental parser preserves bounded partial-frame state, detects the magic/header layout and submits complete frames upward. Truncated input is retained only within the configured receive buffer and frame timeout. Malformed lengths, header CRC and final frame CRC are rejected before local transport processing.

The canonical wire decoder validates complete frame layout. CRC detects transmission errors; authentication policy and verification are enforced at local network delivery. A decoded AUTH flag is not proof of verification.

## Synchronous Transmit Contract

The PHY transmit callback must stop reading its borrowed buffer before returning. Success means the synchronous submission completed, and a failure is returned to the protocol caller. Datalink does not retain a pointer for DMA completion and does not provide an asynchronous ownership token.

A DMA driver must copy into driver-owned storage before return, or wait for completion. Protocol scratch can then be released immediately and is reusable for ACK/control or another frame. Callback execution cannot reenter the instance.

## Limits and Scheduling

The compiled frame-size bound and configured link MTU both apply to the full serialized frame, including extensions, tag and CRC. `xgl_step` receives explicit time and a receive-work budget. `xgl_next_timeout` combines link polling with transport deadlines; parser expiry is checked when its link is polled. An active zero delay means work is due now. Drivers supply readiness notifications or polling for newly arrived bytes.
