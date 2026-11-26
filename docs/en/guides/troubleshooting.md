# Troubleshooting

Start with the returned error, the compiled profile, and the exact configuration/workspace requirements. The protocol does not install an internal logging or platform diagnostics backend.

## Initialization

Check the generated header/library ABI pair, nonzero node/peer/TX capacity, route callbacks, and RX capacity relative to the full frame limit. The workspace address and size must satisfy `xgl_memory_requirements()`.

Keep the configuration and referenced objects alive. `xgl_create()` reserves storage but still needs `xgl_init()`; `xgl_init_static()` performs initialization itself. For no-heap dynamic creation, supply a valid explicit context allocator.

## Send and Receive

| Symptom/status | Check |
| --- | --- |
| `XGL_ERR_WINDOW_FULL` | Peer in-flight window; keep stepping to receive ACKs |
| `XGL_ERR_NO_MEMORY` | Peer/global packet/message/reassembly capacity and admission budgets |
| `XGL_ERR_BUSY` | PHY, application admission, or an already pending peer message |
| `XGL_ERR_ROUTE_NOT_FOUND` | Target and fixed route configuration |
| `XGL_ERR_BUFFER_TOO_SMALL` | Workspace, RX cache, full frame overhead, or message limit |
| `XGL_ERR_UNSUPPORTED` | Compiled profile and requested feature |
| `XGL_ERR_INVALID_VERSION` | Consumer ABI/build identifier or received wire version |
| `XGL_ERR_ACK_TIMEOUT` | Lost traffic, blocked stepping, or a failed reliable scope |

Verify that PHY RX returns zero bytes when empty, never exceeds its buffer capacity, and does not block indefinitely. Ensure serialized frame length fits both endpoint limits. A local send success is not a delivery receipt.

## Diagnostics and Callbacks

Use `xgl_stats_get()`, `xgl_stats_reset()`, and an application `error_callback` under the same serialized instance policy. Record numeric statuses when Boot diagnostics are reduced.

Callbacks cannot reenter or destroy the instance. Queue recovery for after return. Application BUSY must not be “fixed” by discarding a pointer whose data was already accepted into owned protocol storage.

## Recovery

A failed reliable scope requires explicit retirement and a new epoch. Close unauthenticated peers with `xgl_close_peer()` after coordinating stale-traffic draining. Close authenticated sessions through `xgl_close_security_session()` and install fresh trusted parameters.

If failures appear only on hardware, compare the final linker map, call-stack depth, driver partial-send behavior, DMA lifetime, interrupt concurrency, and clock source. The generic footprint ELF proves linking and resource accounting, not board execution.
