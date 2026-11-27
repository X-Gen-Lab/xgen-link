# Migrate to SDK 5

Build SDK 5 from the repository root. Use fresh build directories and install prefixes, then [rebuild each consumer](build-and-consume.md) with the matching generated header and library. Continue linking `xgl::xgl`.

## Protocol and application changes

- **Upgrade each endpoint and relay on the path to Wire v1.** The version field is now `1`. The fixed header layout, CRC coverage, and active flag and TLV assignments are specified in the [Wire v1 reference](../reference/protocol/v1/index.md).
- **Submit one frame's worth of DATA per call.** Remove the previous fragmentation setting and split larger transfers into [application blocks](send.md#application-blocks). The sender returns `XGL_ERR_BUFFER_TOO_SMALL` when a payload exceeds the single-frame budget.
- **Check MTU in both directions.** Verify the DATA path and the ACK return path independently. The receiver reports a missing local reverse route or insufficient feedback capacity before accepting reliable DATA. See [Routing](../reference/protocol/v1/routing.md).

## Configuration and storage

Remove the fragmentation configuration fields, the CMake `fragmentation` component requirement and any references to fragmentation workspace or resource counts. Continue using `XGL_INSTANCE_OPTIONS_INIT` to construct the version 2 advanced options descriptor. It supplies the required size, version and build identity. Scoped callbacks, installed-scope authentication and the reliable TX lifetime option remain available.

Measure aligned storage again with `xgl_memory_requirements()` or `xgl_memory_report_with_options()` before allocating the workspace. Keep the configuration, callbacks and allocator descriptor alive until destruction. The checked entry points validate configuration size, ABI and build identity.

For a custom compiled frame ceiling, set `-DXGL_DATALINK_MAX_FRAME_SIZE=<bytes>` when configuring the library. Reinstall the generated header and library together. See [Interoperability](../reference/protocol/v1/interoperability.md) and [Validation](../development/validation.md) for the release checks.

The SDK 4 migration guide remains available at its [historical revision](https://github.com/X-Gen-Lab/xgen-link/blob/d775c8a2fbc2dabcfc2406034e5cdc43808954bd/docs/en/guides/migration.md).
