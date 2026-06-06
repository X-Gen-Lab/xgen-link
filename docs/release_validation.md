# Release Validation

Release candidates should be validated from a clean build directory before packaging.

## Required Commands

```sh
cmake --preset test
cmake --build build/test
ctest --preset test
cmake --build build/test --target xgl_footprint
cmake --build build/test --target xgl_static_analysis
cmake --build build/test --target xgl_sdk_consumer_smoke
```

The `xgl_release_validation` target groups the available release checks for the configured build:

```sh
cmake --build build/test --target xgl_release_validation
```

## Gates

- Unit, property, integration, echo example, and SDK consumer smoke tests must pass.
- The install package must be consumable through `find_package(xgl CONFIG REQUIRED)`.
- The footprint report must be generated for the selected preset.
- Static analysis must run when `cppcheck` is installed. If it is not installed, the target reports that the baseline is unavailable.
- The worktree should be clean after validation.

