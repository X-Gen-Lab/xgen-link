# Public API

[Generated C API reference](../../api/index.md).

Use `<xgl/xgl.h>`. Installed headers include the generated profile and ABI constants; `internal/` headers are not installed.

## Lifecycle and ownership

`xgl_memory_requirements()` measures exact storage. `xgl_init_static()` initializes aligned caller storage. `xgl_create()` reserves the same layout through `config.memory.allocator` (`xgl_allocator_t`), then requires `xgl_init()`. Libc fallback defaults to enabled for Full and disabled for Embedded/Lite; Embedded/Lite can opt in when rebuilding the SDK. Boot forbids fallback. An explicitly supplied allocator remains the caller's responsibility and may use storage or a heap, so the absence of protocol fallback alone does not prove a product is heap-free. `xgl_destroy()` frees a dynamically reserved workspace, or ends use of static storage.

Use `xgl_create_with_error(&config, &handle)` to diagnose reservation failures: it returns configuration, ABI, backend or allocation errors. Allocation failure returns `XGL_ERR_NO_MEMORY`; every failure clears the handle. Success still only reserves storage and requires `xgl_init()`. The original pointer-returning `xgl_create()` remains compatible and uses the same implementation. The corresponding `_checked` entry accepts the consumer configuration size, ABI and build identity explicitly across compilation boundaries.

`xgl_memory_report()` returns the same requirements with a breakdown by resource category, without allocating or changing an instance. Category bytes include padding and sum to the exact workspace size. Admission byte limits can be smaller than reserved pool storage; see [Resource Model](../explanation/resource-model.md).

The configuration and referenced PHY/provider/callback objects are borrowed until destruction. Keep their addresses stable and do not modify them while the instance exists. All operations on an instance are serialized by the caller; callbacks must not reenter it.

## Send and scheduling

`xgl_send_at(handle, &tx, now_ms)` accepts a borrowed payload during the call and retains reliable data in bounded protocol storage. `xgl_send_zerocopy_at()` uses caller frame storage for unreliable single-frame sends; it does not permit an asynchronous PHY to retain that address.

The legacy zero-copy request always uses connection/epoch zero. `xgl_send_zerocopy_scoped_at()` accepts a separate compatible request with an explicit scope. `xgl_send_zerocopy_layout()` measures exact headroom and total storage without allocating, transmitting or consuming authentication state. See [Send API](../guides/send.md) for preparation and lifetime rules.

`xgl_step(handle, now_ms, &budget)` polls due links and runs transport maintenance. `budget.rx_bytes` is the limit per link, not a frequency. `xgl_next_timeout(handle, now_ms, &delay)` returns whether a relative deadline exists. All calls share one monotonic modulo-2^32 millisecond clock; elapsed intervals must stay below 2^31.

`xgl_next_timeout_for_work(handle, now_ms, &budget, &delay)` queries deadlines for the proposed next step. Zero RX bytes excludes PHY polling, while transport and partial-frame expiry enabled by that budget remain active. The query changes neither the instance nor the latest step's timeout policy. The legacy query still includes all PHY polling. Both queries leave output unchanged when returning false; see [Low-Power Runtime](../guides/low-power.md).

## Sessions and diagnostics

`xgl_install_security_session()` copies explicitly trusted directional parameters. `xgl_close_security_session()` closes the association and releases the matching transport peer while retaining the security tombstone. `xgl_close_peer()` cancels an unauthenticated scope; drain old traffic and use a new epoch before reconnecting.

`xgl_stats_get()` / `xgl_stats_reset()` require exclusive instance access. `xgl_error_string()` translates the protocol error domain. `xgl_version_string()` and `xgl_version_int()` report the linked SDK version.

See [migration](../guides/migration.md) and [security](protocol/v3/security.md).
