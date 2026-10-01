# Boot update host simulation

This example exercises a bounded firmware-transfer application with wire v3 and
static protocol workspaces. It contains no MCU Flash driver, RTOS dependency,
heap allocation, signature verification, or secure-boot claim.

The application protocol has two messages:

| Data type | Payload | Meaning |
| --- | --- | --- |
| 7 | little-endian 32-bit offset plus 64 image bytes | Submit one fixed block |
| 8 | little-endian 32-bit committed byte count | Device confirms simulated Flash commit |

The device begins with a 25 ms erase delay. Its receive admission callback returns
`XGL_ERR_BUSY` until Flash can accept a staging copy. Accepted data is copied into
one 64-byte staging slot. The main loop completes a simulated write after 20 ms
and sends the commit notification outside the callback. The host keeps at most
one uncommitted application block in flight and waits for that notification before
submitting the next one. The transport window may still be full until its ACK
arrives; the host handles that status without losing the pending block.

The PHY deliberately drops the first ACK after a block is accepted. The host
retransmits, transport suppresses duplicate application delivery, and the device
sends another ACK. The program requires exactly eight Flash writes, at least one
BUSY result and retransmission, one dropped ACK, matching image bytes, and a
matching core CRC16/MODBUS checksum before printing PASS. A successful send means
submission was accepted, not that Flash has committed it. Protocol failures are
reported through the error callback and make the final check fail.

Build and run the Boot profile as pure C:

```sh
cmake -S . -B build/boot-example -G Ninja -DCMAKE_C_COMPILER=gcc   -DXGL_PROFILE=boot -DXGL_BUILD_TESTS=OFF -DXGL_BUILD_EXAMPLES=ON   -DXGL_ALLOW_FALLBACK_MALLOC=OFF -DXGL_BUILD_NOHEAP_SMOKE=ON
cmake --build build/boot-example --target boot_update
./build/boot-example/examples/boot_update/boot_update
```

The full-profile `gcc-test` build also provides `boot_update`. On Windows append
`.exe`. Its deterministic clock is a caller-supplied `uint32_t` millisecond value;
no wall-clock service or sleeping is required.

Configuration, routes, descriptors, callback state, and aligned workspace remain
valid until both instances are destroyed. The shared example helper reserves a
generous host array and queries the exact selected-profile workspace requirement.
These arrays are not MCU resource claims.

CRC16 detects accidental corruption; it is not a cryptographic image hash or an
authentication mechanism. Production firmware update still needs a signed image
manifest, trusted key provisioning, anti-rollback policy, power-failure-safe image
activation, board-specific Flash/DMA ownership, and measured target resources.
The Boot profile has authentication disabled. This simulation demonstrates transfer
and flow control only; it does not solve those board/product responsibilities.
