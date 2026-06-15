# Public API

Normal SDK users depend only on installed public headers:

- `xgl.h`
- `xgl_config.h`
- `xgl_types.h`
- `xgl_error.h`

Internal protocol headers live under `include/xgl/internal`, for example `xgl/internal/xgl_wire.h`, `xgl/internal/xgl_parser.h`, and `xgl/internal/xgl_reliable.h`. They are not installed as normal SDK headers and are not stable user APIs.

## API Groups

- Lifecycle: `xgl_create`, `xgl_init`, `xgl_destroy`
- Send: `xgl_send`, `xgl_send_zerocopy`
- Runtime: `xgl_run`, `xgl_next_deadline_ms`
- Stats: `xgl_stats_get`, `xgl_stats_reset`
- Version: `xgl_version_string`, `xgl_version_int`

## Doxygen

The CMake documentation build generates the public C API reference:

<a href="../../../api/doxygen/html/index.html">Open generated Doxygen API</a>
