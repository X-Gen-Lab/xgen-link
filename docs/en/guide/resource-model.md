# Resource Model

XGL targets bounded embedded systems. Production delivery must explain peak memory, runtime allocation, and queue budgets.

## Allocation Phases

| Phase | Description |
| --- | --- |
| init | Instance, route, parser, reliable, fragment, replay window |
| TX | Normal single-frame sends should avoid runtime allocation |
| RX | Normal single-frame receives should avoid runtime allocation |
| reliable | Retransmission data and queue nodes |
| fragment | Fragment arrays, reassembly buffers, and ranges |

## No-Heap Profile

When `XGL_ALLOW_FALLBACK_MALLOC=OFF`, a NULL allocator must fail closed. `xgl_noheap_smoke` validates strict profile behavior.

## Preset Resources

| Preset | TX Pool | RX Buffer | Window | Max Frame | Fragment |
| --- | ---: | ---: | ---: | ---: | --- |
| Tiny | 1024 | 160 | 2 | 128 | off |
| Small | 2048 | 288 | 4 | 256 | on |
| Medium | 4096 | 544 | 8 | 512 | on |
| Large | 8192 | 1056 | 16 | 1024 | on |
| Production | 8192 | 1056 | 16 | 1024 | on + auth required |

These are SDK starting points, not board-certified values. Final values must come from target link MTU, payload size, window, fragment concurrency, and authentication tag length.

## Production Checklist

- allocator call counts
- TX/RX peak usage
- reliable queue peak usage
- reassembly budget peak usage
- stack high-water mark
- footprint report

## Runtime Determinism

The strict production profile aims for no allocator calls after init for normal single-frame TX/RX. Reliable transport and fragmentation may need additional pool resources; if the target forbids runtime allocation, fixed pools must cover reliable packets, RX buffered packets, and reassembly buffers.
