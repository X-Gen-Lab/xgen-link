# Production Checklist

Acceptance applies to a specific configuration, target ABI, driver, and provider. Preserve those inputs with the resulting artifacts.

## Build and Configuration

- Let the product prepare and record revisions, dirty states, or installation provenance for status, bytes, CRC, memory, and containers. Product gitlinks pin versions when submodules are used. Link consumes provided targets or installed packages and does not use its development manifest to select product dependencies.
- Build library and consumers with matching profile/ABI headers.
- Validate explicit capacities and the exact workspace size/alignment.
- Keep borrowed configuration and contexts immutable and alive until destroy.

## I/O and Execution

- Serialize all instance calls; callbacks never reenter or destroy the instance.
- Verify synchronous whole-frame PHY acceptance, including DMA adapters.
- Verify nonblocking bounded RX and per-link parser separation.
- Use one monotonic millisecond clock for send, step, and timeout queries.

## Reliability and Recovery

- Exercise ACK loss, duplicates, reorder, full windows, and PHY BUSY.
- Exercise application BUSY with already retained reliable/fragmented data.
- Verify fragmented messages larger than the window and global resource exhaustion.
- Retire failed scopes explicitly and reconnect under a new epoch; drain stale unauthenticated traffic.
- Treat local acceptance separately from remote application completion.

## Security

- Use the selected profile's trusted-session API and a reviewed provider.
- Authenticate complete provider AAD and payload with the supplied directional nonce/key identity.
- Ensure restart freshness and no key/nonce-domain reuse.
- Respect session capacity and retained tombstones; received frames cannot establish trust.
- Keep reserved compression and payload-encryption features disabled.

## Memory and Timing

- Inspect the final ELF for unexpected heap services.
- Include workspace, application/driver buffers, provider state, BSP, and stack in RAM.
- Measure full call chains and ISR nesting on target; single-function reports are insufficient.
- Verify overflow rejection, capacity recovery, clock wrap, and worst-case step duration.
- Confirm product Flash/RAM limits with its actual linker map.

## Release Evidence

Archive build arguments, compiler version, dependency revisions, map/ELF, test reports, and hardware observations. The standalone Boot consumer is link/resource evidence and does not replace a real board test or boot-update power-loss validation.
