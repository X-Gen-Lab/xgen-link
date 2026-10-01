# Build and test

## Production module dependencies

```sh
cmake --preset gcc-test \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
```

Install status, bytes, CRC, memory, and containers into the SDK prefix first. The GoogleTest 1.16.0 installation must include GoogleMock. See [modular migration](../guide/modular-migration.md) for package versions and targets. The production root accepts installed packages or compatible parent-provided targets only. It neither downloads dependencies nor selects source checkouts, and has no default `external` submodules.

The product supplies one target set per package before adding link with `add_subdirectory` and linking `xgl::xgl`. The protocol does not recursively fetch a second production dependency set. Examples, smoke tests, and release helpers default to disabled when consumed as a subdirectory; select any required development checks explicitly.

## Standalone source development

```sh
cmake -S dev -B build/dev-full -G Ninja \
  -DXGL_DEV_STATUS_SOURCE_DIR=/path/to/xgen-status \
  -DXGL_DEV_BYTES_SOURCE_DIR=/path/to/xgen-bytes \
  -DXGL_DEV_CRC_SOURCE_DIR=/path/to/xgen-crc \
  -DXGL_DEV_MEMORY_SOURCE_DIR=/path/to/xgen-memory \
  -DXGL_DEV_CONTAINERS_SOURCE_DIR=/path/to/xgen-containers \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build build/dev-full
ctest --test-dir build/dev-full --output-on-failure
```

Supply all five paths explicitly as absolute paths. The compiler, architecture, and GoogleTest installation must match. The development harness supplies foundation targets before consuming the same production link module. `dev/dependencies.json` records fixed development/CI inputs; products do not read it to select versions. See the repository's `dev/README.md` for defaults, preparation, and profile commands.

The production `XGL_{STATUS,BYTES,CRC,MEMORY,CONTAINERS}_SOURCE_DIR` options have been removed and fail configuration. Configure `dev` with its `XGL_DEV_*` arguments, or install dependencies before configuring the root. Use a new build directory instead of reusing a root-project cache.

## Full regression

```sh
cmake --preset gcc-test \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset gcc-test
ctest --preset gcc-test
```

These commands use installed dependencies; the development harness above is the source alternative. Production libraries are C11, and protocol GoogleTest uses strict C++17. Test dependencies are excluded from production exports.

## Bounded profiles

```sh
cmake --preset boot -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset boot
ctest --preset boot
cmake --preset embedded-lite -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset embedded-lite
ctest --preset embedded-lite
cmake --preset embedded -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset embedded
ctest --preset embedded
```

All three disable protocol libc fallback and default to checking a static lifecycle plus an installed pure C consumer. For host behavior regression, explicitly add `-DXGL_BUILD_TESTS=ON -DGTest_DIR=/path/to/gtest/lib/cmake/GTest`. Boot/Lite select tests by compiled capability, while Embedded runs the full regression suite. Lite retains fragmentation/out-of-order buffering and removes authentication/forwarding.

For source development use the corresponding dev preset, supplying all five source-path environment variables or explicit `-D` arguments, for example `cmake -S dev --preset boot -DXGL_BUILD_TESTS=ON`. Keep profiles in separate directories; do not reuse Full caches or generated headers. The development harness adds the libc allocator target needed by host tests, but the Boot protocol does not link it.

## Install

```sh
cmake --install build/gcc-test --prefix ./install
```

Install the five dependencies separately into a common prefix or prefixes listed in `CMAKE_PREFIX_PATH`; installing link does not install its dependencies. Consumers use `find_package(xgl CONFIG REQUIRED)` and `xgl::xgl`. The package resolves required components and checks versions and ABI. Use the installed generated profile header; checked entry points reject ABI/profile mismatches.

Consumers may request `find_package(xgl CONFIG REQUIRED COMPONENTS fragmentation out_of_order)` to reject insufficient package capabilities during configuration. See [configuration](../guide/configuration.md) for capability names and profile identities.

## Multi-configuration generators

Pass the same explicit configuration to Ninja Multi-Config or Visual Studio builds:

```sh
cmake --build build/multi --config Release --target xgl_release_validation
ctest --test-dir build/multi -C Release --output-on-failure --no-tests=error
cmake --install build/multi --config Release --prefix ./install-release
```

Release helpers pass the configuration through CTest, nested SDK builds and installation, and isolate consumer directories by configuration. `test/cmake/test_profile_builds.py` builds both Debug/Release consumers and checks their corresponding installed target files.
