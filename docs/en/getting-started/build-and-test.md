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

For explicitly requested native GoogleTest regression, use the host-test presets:

```sh
cmake --preset boot-host-test -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset boot-host-test
ctest --preset boot-host-test
```

Use `embedded-lite-host-test` or `embedded-host-test` for the other bounded profiles. These presets enable GoogleTest and select protocol tests by the `xgl` label; the original profile presets remain production/smoke builds. The same names exist under `dev/`, with the five explicit source paths. All test presets reject an empty selection. The shared quality runner in the release commands additionally validates discovery, labels and execution of every selected smoke and example.

## Compiled resource options

The product selects the following resource combination when compiling the protocol library. The production root and `dev` harness accept the same options. They determine actual object layout and behavior; consumer macros cannot resize an installed library.

| CMake option | Supported values and default | Effect |
| --- | --- | --- |
| `XGL_RELIABLE_INDEX_BUCKETS` | `AUTO`, `1`, `2`, `4`, `8`, `16`, `32`; default `AUTO` | Reliable queue index buckets per peer; fewer buckets reduce fixed storage, while longer bucket chains can increase lookup cost |
| `XGL_TRANSPORT_WINDOW_CAPACITY` | `AUTO`, `1`, `8`, `16`, `32`, `64`, `128`; default `AUTO`, resolved to `128` | Compiled private window capacity and embedded acknowledgement bitmap |
| `XGL_ENABLE_ADAPTIVE_RTO` | `ON`, `OFF`; default `ON` | Retain peer RTT estimators and ACK sampling; `OFF` uses the configured or request-specific ACK timeout |

Index bucket `AUTO` defaults are `1` for Boot, `8` for Embedded Lite and `32` for Embedded/Full. Compiled window capacity and runtime `protocol.window_size` are separate selections: the public runtime window remains `1–32` and cannot exceed compiled capacity. Default private capacity `128` preserves the historical contract without enlarging the public API window limit. Disabling adaptive RTO retains per-request timeout overrides and exponential retry backoff; see [reliability](../protocol/reliability.md#transmit-lifecycle).

For example, after installing the five dependency packages, build a Boot SDK with a one-packet window and fixed RTO:

```sh
cmake -S . -B build/boot-compact -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DXGL_PROFILE=boot \
  -DXGL_RELIABLE_INDEX_BUCKETS=1 \
  -DXGL_TRANSPORT_WINDOW_CAPACITY=1 \
  -DXGL_ENABLE_ADAPTIVE_RTO=OFF \
  -DXGL_BUILD_TESTS=OFF -DXGL_BUILD_EXAMPLES=OFF
cmake --build build/boot-compact
cmake --install build/boot-compact --prefix ./install-boot-compact
```

This selects a build combination; it does not prove compliance with a Flash, RAM or stack budget. The product must also configure runtime capacities and validate its complete target ELF and board peak usage. `test/cmake/test_transport_resource_config.py` verifies supported combinations, window boundaries, conflict rejection and installed consumption. `test/unit/transport/test_transport_fixed_rto.cpp` verifies fixed RTO behavior.

## Build identities and SDK consistency

When all resolved resources match profile defaults, `XGL_BUILD_CONFIG_ID` remains `301` for Boot, `302` for Embedded, `303` for Full and `304` for Embedded Lite. A nondefault combination receives a distinct identity incorporating index buckets, window capacity, adaptive RTO, resolved PHY read chunk size and libc fallback selection. `XGL_DATALINK_RX_CHUNK_SIZE` accepts `AUTO/16/32/64/128`; `AUTO` resolves to `32` for Boot and `128` for other profiles. Libc fallback defaults to enabled for Full and disabled for the other profiles; Boot requires it disabled.

The generated header also provides the compiled resource identity. CMake `xgl::xgl` exports matching properties, and the installed package exposes `xgl_BUILD_CONFIG_ID`, `xgl_RESOURCE_CONFIG_ID` and resolved resource options. Package configuration rejects an identity mismatch with an existing target; target compatibility requirements reject conflicting consumer combinations; generated headers reject macro overrides that disagree with compiled resources; public checked entry points reject caller/runtime identity mismatches. Consumers must use the target and generated header from the same SDK and pass generated identities through public entry points instead of hardcoding `301–304` from the profile name.

Use a separate build directory and SDK prefix when changing the resource combination, then rebuild consumers. Setting these CMake options while consuming an installed package does not resize the library: rebuild and select the matching SDK. See [memory and capacity](../protocol/memory.md#static-workspace-layout) for querying that build's actual workspace requirements.

## Install

```sh
cmake --install build/gcc-test --prefix ./install
```

Install the five dependencies separately into a common prefix or prefixes listed in `CMAKE_PREFIX_PATH`; installing link does not install its dependencies. Consumers use `find_package(xgl CONFIG REQUIRED)` and `xgl::xgl`. The package resolves required components and checks versions and ABI. Use the installed generated profile and resource configuration header; checked entry points reject ABI/profile/compiled resource identity mismatches.

Consumers may request `find_package(xgl CONFIG REQUIRED COMPONENTS fragmentation out_of_order)` to reject insufficient package capabilities during configuration. See [configuration](../guide/configuration.md) for capability names and profile identities.

## Multi-configuration generators

Pass the same explicit configuration to Ninja Multi-Config or Visual Studio builds:

```sh
cmake --build build/multi --config Release --target xgl_release_validation
ctest --test-dir build/multi -C Release --output-on-failure --no-tests=error
cmake --install build/multi --config Release --prefix ./install-release
```

Release helpers pass the configuration through CTest, nested SDK builds and installation, and isolate consumer directories by configuration. `test/cmake/test_profile_builds.py` builds both Debug/Release consumers and checks their corresponding installed target files.
