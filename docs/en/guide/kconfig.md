# Kconfig and Build Configuration

CMake is the repository's only configuration generation entry point. The project does not ship a separate Kconfig-to-header generation pipeline.

## Compiled Configuration

Set `XGL_PROFILE` to `boot`, `embedded`, or `full`. CMake emits `generated/xgl/xgl_build_config.h` with capability, ABI, and build identifiers. Consume the exported `xgl::xgl` target or installed package so the matching generated header is used.

Set `XGL_ALLOW_FALLBACK_MALLOC=OFF` and `XGM_BUILD_LIBC_ALLOCATOR=OFF` when the production image must not contain libc allocation. The test configuration may build additional allocator support; measure a separate production build.

## Host RTOS Mapping

A host RTOS can expose its own Kconfig choices and explicitly translate them into CMake cache arguments before adding xgen-link. For example, a host boot choice maps to `-DXGL_PROFILE=boot`. That translation belongs to the host build and must be tested there.

Do not define capability macros in one application source while linking a library built with a different profile. Do not expect a `.config` file to change this repository's generated header automatically.

## Runtime Configuration

`xgl_config_t` selects supported features, routes, timeouts, and fixed resource capacities within the compiled capability ceiling. Validate it before querying or preparing the workspace. Runtime settings cannot enable a compiled-out feature.

See [Configuration](configuration.md) for field meanings and [Resource Model](resource-model.md) for sizing.
