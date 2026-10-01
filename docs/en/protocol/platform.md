# Platform and Profiles

The portable protocol core requires a C toolchain and five independent foundation packages; see [modular migration](../guide/modular-migration.md) for versions and entry points. The application supplies explicit time, PHY callbacks, storage and serialized access. The library has no global time provider, OS mutex subsystem or automatic heap fallback.

## Execution Contract

Use `xgl_send_at(handle, data, now_ms)`, `xgl_step(handle, now_ms, budget)` and `xgl_next_timeout(handle, now_ms, &delay)`. All calls use one monotonic millisecond domain modulo 2^32; relevant intervals and elapsed comparisons must stay below the half range. A timeout result has an explicit active boolean, so zero is a valid due-now delay.

The work budget bounds receive bytes per link and supplies the receive timeout. Protocol work is additionally bounded by configured peers, packets and messages. Newly arrived RX data and restored application receive capacity must cause another step. Local TX BUSY retries have a positive bounded timer. The caller serializes operations on each instance; callbacks cannot reenter or destroy it. Multiple instances can be scheduled independently.

## Compiled Profiles

| Capability | Boot | Embedded | Full |
| --- | --- | --- | --- |
| Wire version | 3 | 3 | 3 |
| Authentication | Excluded | Included | Included |
| Security session slots, including closed tombstones | 0 | 4 | 16 |
| Fragmentation and out-of-order storage | Excluded | Included | Included |
| Forwarding and route index | Excluded | Included | Included |
| Reliable index buckets | 1 | 32 | 32 |
| Build configuration ID | 301 | 302 | 303 |

Included features still require valid runtime configuration and explicit capacities. All profiles reject unsupported compression/encryption. Build-specific public headers and the compiled library must match; ABI/profile checks reject mismatches.

## Small Boot Deployment

A small Boot configuration uses a static workspace, one link, one route, one peer, one reliable TX record and window one. An MTU of 128 bytes is a useful baseline. A Full peer communicating with it must use wire v3, compatible authentication policy, no generic fragmentation and a one-packet sending window. The protocol does not automatically negotiate these capacities.

Firmware transfer uses bounded application blocks. A transport ACK only acknowledges acceptance; the Boot application emits a separate durable-commit response after flash programming/verification. The application defines image identity, offsets, resume state, retry policy and final verification. A 64 KB Flash / 8 KB RAM target must be checked using the actual target link map, static workspace requirement and worst-case stack; host sizes do not prove that budget.
