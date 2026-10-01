# Public API

Use `<xgl/xgl.h>`. Installed headers include the generated profile and ABI constants; `internal/` headers are not installed.

## Lifecycle and ownership

`xgl_memory_requirements()` measures exact storage. `xgl_init_static()` initializes aligned caller storage. `xgl_create()` reserves the same layout through `config.memory.allocator` (`xgm_allocator_t`), then requires `xgl_init()`. Only Full may select the optional xgen-memory libc backend. `xgl_destroy()` frees a dynamically reserved workspace, or ends use of static storage.

The configuration and referenced PHY/provider/callback objects are borrowed until destruction. Keep their addresses stable and do not modify them while the instance exists. All operations on an instance are serialized by the caller; callbacks must not reenter it.

## Send and scheduling

`xgl_send_at(handle, &tx, now_ms)` accepts a borrowed payload during the call and retains reliable data in bounded protocol storage. `xgl_send_zerocopy_at()` uses caller frame storage for unreliable single-frame sends; it does not permit an asynchronous PHY to retain that address.

`xgl_step(handle, now_ms, &budget)` polls due links and runs transport maintenance. `budget.rx_bytes` is the limit per link, not a frequency. `xgl_next_timeout(handle, now_ms, &delay)` returns whether a relative deadline exists. All calls share one monotonic modulo-2^32 millisecond clock; elapsed intervals must stay below 2^31.

## Sessions and diagnostics

`xgl_install_security_session()` copies explicitly trusted directional parameters. `xgl_close_security_session()` closes the association and releases the matching transport peer while retaining the security tombstone. `xgl_close_peer()` cancels an unauthenticated scope; drain old traffic and use a new epoch before reconnecting.

`xgl_stats_get()` / `xgl_stats_reset()` require exclusive instance access. `xgl_error_string()` translates the protocol error domain. `xgl_version_string()` and `xgl_version_int()` report the linked SDK version.

See [migration](../guide/modular-migration.md) and [security](../protocol/security.md).
