# Build and consume

Build the production library with a C11 compiler and CMake 3.24 or later. Run these commands from the xgen-link repository root. The default build produces the static library and matching configuration header.

```sh
cmake -S . -B build/production
cmake --build build/production
cmake --install build/production --prefix "$PWD/build/sdk"
```

Inspect the install prefix for the public headers, the static library and `xglConfig.cmake`. Use the generated header and library from the same build. Use separate build directories and prefixes for different compilers, target ABIs, profiles or resource selections. With a multi-config generator, use the same `--config` for build and install. Install Debug and Release into separate prefixes.

## CMake consumption

For source composition, the parent selects the toolchain and [build profile](../reference/profiles.md), then calls `add_subdirectory(path/to/xgen-link)`. Use the following entry for an installed SDK. Both entries provide `xgl::xgl`.

```cmake
find_package(xgl 4 CONFIG REQUIRED COMPONENTS wire reliable)
target_link_libraries(my_application PRIVATE xgl::xgl)
```

Set `CMAKE_PREFIX_PATH` to the SDK install prefix when configuring the consumer. Installed packages accept `COMPONENTS` for compiled capabilities. All profiles support `wire` and `reliable`. Compile and run the actual consumer after it configures successfully.

Repeat a lookup in the same CMake scope using the already bound SDK prefix. To switch prefixes, configure a fresh consumer build so the archive and headers of `xgl::xgl` both come from the selected SDK.

## Compiled resource options

The bare-metal Boot example configuration uses `cmake --preset boot` with Ninja. For a compiled capacity of one window slot and fixed RTO, choose the resources explicitly in a separate build directory:

```sh
cmake -S . -B build/boot-small -G Ninja -DXGL_PROFILE=boot \
  -DXGL_TRANSPORT_WINDOW_CAPACITY=1 -DXGL_RELIABLE_INDEX_BUCKETS=1 \
  -DXGL_ENABLE_ADAPTIVE_RTO=OFF
cmake --build build/boot-small
```

See [Build profiles](../reference/profiles.md) for the resource identity and window, reliable index, frame ceiling, read chunk and advanced runtime options. For real hardware, also select the cross compiler and the application capacities.

## Host validation

Prepare an installation of GoogleTest 1.16.0 with GoogleMock and use a fresh native build directory. Configuration uses the provided tools and installed packages.

```sh
cmake --preset gcc-test -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset gcc-test
ctest --preset gcc-test --no-tests=error
```

Use `boot-host-test`, `embedded-lite-host-test` or `embedded-host-test` for the corresponding capabilities. C11 examples, no-heap checks, documentation and release validation each require explicit selection. See the [development workflow](../development/workflow.md) for the complete verification path.
