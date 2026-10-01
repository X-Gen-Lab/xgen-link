# Datalink

Datalink separates PHY byte I/O from protocol packets. It does not own reliability, peer identity or trusted sessions.

## Receive Framing

PHY receive bytes may end in the middle of a header or frame, or contain several frames. The incremental parser preserves bounded partial-frame state, detects the magic/header layout and submits complete frames upward. Truncated input is retained only within the configured receive buffer and frame timeout. Malformed lengths, header CRC and final frame CRC are rejected before local transport processing.

The canonical wire decoder validates complete frame layout. CRC detects transmission errors; authentication policy and verification are enforced at local network delivery. A decoded AUTH flag is not proof of verification.

Once the fixed header validates, the parser immediately checks that the header, payload and CRC minimum fits its cache. After decoding SECURITY, it checks the authentication trailer too. Impossible candidates are dropped and later bytes continue searching for magic. In `A5 A5 5A`, the second `A5` starts a new candidate and timestamp; normal header and payload progress never extends its deadline.

The private `xgl_datalink_consume_span()` synchronously consumes stable bytes across arbitrary frame boundaries, including several frames in one span. Completed views borrow the parser cache until delivery returns, then parsing resets and continues. BUSY does not retain that borrowed pointer; the function returns the first delivery error. Input must not overlap the cache or be modified by DMA during consumption. The function retains no input pointer, reads no PHY and does not check expiry itself.

`poll_parser` owns expiry and one PHY read before calling the same span entry. Direct complete-frame input obtains its view and CRC status from wire without an extra metadata wrapper. These are private implementation interfaces. Applications still drive `xgl_step`; this does not introduce an ISR API, asynchronous DMA ownership or a zero-copy receive guarantee.

## Synchronous Transmit Contract

The PHY transmit callback must stop reading its borrowed buffer before returning. Success means the synchronous submission completed, and a failure is returned to the protocol caller. Datalink does not retain a pointer for DMA completion and does not provide an asynchronous ownership token.

A DMA driver must copy into driver-owned storage before return, or wait for completion. Protocol scratch can then be released immediately and is reusable for ACK/control or another frame. Callback execution cannot reenter the instance.

## Limits and Scheduling

The compiled frame-size bound and configured link MTU both apply to the full serialized frame, including extensions, tag and CRC. `xgl_step` receives explicit time and a receive-work budget. `xgl_next_timeout` combines link polling, parser and transport deadlines. Partial-frame expiry is maintained even with zero RX budget or before the next PHY poll. An active zero delay means work is due now. Drivers supply readiness notifications or polling for newly arrived bytes.

The build option `XGL_DATALINK_RX_CHUNK_SIZE` accepts `AUTO`, `16`, `32`, `64` and `128`. The default `AUTO` selects 32 bytes for Boot and 128 for other profiles. The installed configuration header records the resolved value; consumer `-D` overrides cannot change an already compiled library. Each due poll reads at most `min(rx_bytes, chunk)` bytes in one PHY call. Reducing the runtime budget does not shrink the compiled stack array; reducing the chunk changes neither MTU nor persistent cache capacity.

Products must validate polling rate, driver buffer capacity and total frame timeout together. Reading 32 bytes every millisecond has an ideal continuous ceiling of 32,000 B/s, before scheduling and burst margin. Select 64 or 128 explicitly when needed. See [Datalink Optimization](../reference/datalink-optimization.md) for measured resources and validation limits.
