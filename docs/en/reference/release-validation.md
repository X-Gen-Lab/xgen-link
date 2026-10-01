# Release validation

## Prepare inputs

Install the five foundation packages as described in [Build and test](../getting-started/build-and-test.md). Prepare GoogleTest/GoogleMock 1.16.0, the xgen-quality commit pinned by `tools/quality.json`, and documentation dependencies from `docs/requirements.txt`. Provide the Cppcheck, Clang-Tidy, and Doxygen versions required by the quality package; fix missing or mismatched tools during preparation.

The root-project commands below consume installed packages. The `ci` preset selects native GNU, Debug, Full, and production `xgl` coverage. Use a separate build directory so counters do not belong to different sources or configurations.

## Local gates

```sh
python tools/quality.py text
python tools/quality.py format
python -m pre_commit run --all-files
cmake --preset ci \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset ci --parallel 2
python tools/quality.py test --build-dir build/ci
python tools/quality.py tidy --build-dir build/ci
python tools/quality.py docs
python tools/quality.py coverage --build-dir build/ci --gcov-executable gcov
cmake --build build/ci --target xgl_release_validation --parallel 2
pwsh -File tools/docs_qa.ps1
```

The shared test entry compares CTest discovery with actual JUnit results. Line, function, and branch coverage must each reach 80%. Reports are retained in `out/reports/`. New or fixed behavior also requires TDD RED/GREEN evidence; historical coverage additions and documentation changes use appropriate verification.

`xgl_release_validation` builds and runs protocol tests, examples, the SDK consumer, static workspace and applicable noheap smoke checks. It depends on the shared Cppcheck target, footprint report, and documentation site. The independent runner commands above additionally enforce text, Clang-Tidy, strict API documentation, and coverage gates. `tools/docs_qa.ps1` checks bilingual structure and obsolete API references.

For multi-configuration generators use `cmake --build <dir> --config Release --target xgl_release_validation`. The target passes Release through CTest and nested SDK build/install commands, isolating Debug/Release consumer directories. Direct CTest execution still requires `-C Release`.

For source development, provide five paths as described in the repository's `dev/README.md`, configure with `cmake -S dev --preset ci`, and replace the build directory with `build/dev-ci`. Production subdirectories do not add these helpers by default; products explicitly enable the required checks. The dev manifest fixes protocol-repository test inputs only.

## Configuration matrix

```sh
cmake --preset boot -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk -DXGL_BUILD_TESTS=ON
cmake --build --preset boot
python tools/quality.py test --build-dir build/boot --config MinSizeRel
cmake --preset embedded-lite -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk -DXGL_BUILD_TESTS=ON
cmake --build --preset embedded-lite
python tools/quality.py test --build-dir build/embedded-lite --config MinSizeRel
cmake --preset embedded -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk -DXGL_BUILD_TESTS=ON
cmake --build --preset embedded
python tools/quality.py test --build-dir build/embedded --config MinSizeRel
```

These native host configurations require compatible GoogleTest/GoogleMock 1.16.0, supplied through `GTest_ROOT` or `GTest_DIR`, plus the foundation capabilities used by tests. Boot/Lite/Embedded build separately without protocol libc fallback. Boot/Lite validate their available capabilities, while Embedded runs complete regression. Scripts in `test/cmake` check source reuse, installation consumption, capability/dependency rejection, Debug/Release publication, C++17, discovery, and replay. CI includes these fixed checks.

Preserve environment-specific reports for native Host, ASan/UBSan, bounded profiles, package consumption, and coverage. [GitHub CI](github-ci.md) describes jobs and fixed preparation. Release evidence identifies source/dependency commits, tools, configurations, and results. A remote pass requires an actual run for the target commit.

## Product acceptance

Run the Cortex-M0 final-ELF footprint probe separately, then integrate the board. Account for full-image Flash, static RAM, stack, PHY buffers, and the real Boot partition. Verify Flash erase/program timing and power-loss recovery. Authentication requires a production provider and persistent freshness or fresh trusted keys. Report host simulation, SDK consumption, and ELF linkage within their respective scope; see [validation matrix](validation-matrix.md) for board acceptance.

CI uses locked Arm GNU 15.2.rel1/GCC 15.2.1 to build MTU/RX 128 and 64 separately, enforce each resource policy, and report Flash, workspace, static RAM, reserved stack, and largest single-function stack deltas. Heap symbols, unresolved symbols, wrong configurations, and missing measurements fail. Limits qualify only the fixed probe, not board timing or summed call-chain stack. New workflow checks require a remote result for the current commit before being reported as CI passes.
