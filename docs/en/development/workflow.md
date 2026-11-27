# Development workflow

Use a C11 production build and strict C++17 host tests. Prepare CMake, the selected compiler and the test dependencies before configuring the build.

## Behavior and regressions

When changing behavior or fixing a defect, run a test through the affected production path, record its actual failure, then implement and rerun the test. Prefer substituting the external PHY, clock or allocator to control inputs. When moving equivalent behavior, preserve historical cases, labels, property seeds and comparison records.

From the repository root, configure and build the host tests as shown in [Build and consume](../guides/build-and-consume.md), then run:

```sh
ctest --preset gcc-test --no-tests=error
```

CI host jobs record and verify the test inventory and JUnit results with `python test/cmake/run_ctest.py --build-dir build/debug --config Debug`. Retain the original error and reproduction command on failure. Use `XGL_PROPERTY_SEED` to set property-test inputs.

The `status` job in CI summarizes the host, profile and SDK checks. The `Build documentation site` job produces the validated artifact used for deployment.

## Documentation

Use the separate CMake build in [Maintain documentation](documentation.md) to verify the bilingual site and generated public API reference.

## Completion

Record the source baseline, toolchain, profile, commands, exit status and actual artifacts. Choose [validation scope](validation.md) based on the lifecycle, wire or resource boundaries affected by the change. Run focused checks first and the complete required matrix before delivery.
