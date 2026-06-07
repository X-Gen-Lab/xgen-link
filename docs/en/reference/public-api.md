# Public API

Normal SDK users depend only on installed public headers:

- `xgl.h`
- `xgl_config.h`
- `xgl_types.h`
- `xgl_error.h`

Internal protocol headers such as `xgl_wire.h`, `xgl_parser.h`, and `xgl_reliable.h` are not normal user APIs.

## API Groups

- Lifecycle: `xgl_create`, `xgl_init`, `xgl_destroy`
- Send: `xgl_send`, `xgl_send_zerocopy`
- Runtime: `xgl_run`, `xgl_next_deadline_ms`
- Stats: `xgl_stats_get`, `xgl_stats_reset`
- Version: `xgl_version_string`, `xgl_version_int`

## Doxygen

The CMake documentation build generates the public C API reference:

[Open generated Doxygen API](/api/doxygen/html/index.html)
