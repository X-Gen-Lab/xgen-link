# Development workflow

Use a C11 production build and strict C++17 host tests. Prepare the shared `xgen-quality` revision named in `tools/quality.json` and the tools required by the selected checks. Configuration and compilation use preinstalled dependencies.

## Behavior and regressions

When changing behavior or fixing a defect, run a test through the affected production path, record its actual failure, then implement and rerun the test. Prefer substituting the external PHY, clock or allocator to control inputs. When moving equivalent behavior, preserve historical cases, labels, property seeds and comparison records.

From the repository root, configure and build the host tests as shown in [Build and consume](../guides/build-and-consume.md), then run:

```sh
python tools/quality.py text
python tools/quality.py format
python tools/quality.py test --build-dir build/gcc-test
```

The shared runner checks the CTest inventory and actual JUnit results. CI host jobs record and verify every CTest execution with `python test/cmake/run_ctest.py --build-dir build/debug --config Debug`, independently of shared quality tool installation. Retain the original error and reproduction command on failure. Use `XGL_PROPERTY_SEED` to set property-test inputs.

Use `status` in `CI` and `Build documentation site` in `Documentation` as merge gates. Deployment reuses the site artifact from the validated documentation build. The lint and analysis jobs read the quality tool source and version from `tools/quality.json`.

## Analysis and documentation

Use the actual compilation database for Cppcheck or clang-tidy. Prepare the tools explicitly and run `python tools/quality.py cppcheck --build-dir <dir>` or `python tools/quality.py tidy --build-dir <dir>` from the root. Record exceptions under the [engineering conventions](../../standards.md). Use the separate checks in [Maintaining documentation](documentation.md) for the site.

## Completion

Record the source baseline, toolchain, profile, commands, exit status and actual artifacts. Choose [validation scope](validation.md) based on the lifecycle, wire or resource boundaries affected by the change. Run focused checks first and the complete required matrix before delivery.
