# Static analysis

## Run

Prepare the five foundation packages and GoogleTest/GoogleMock as described in [Build and test](../getting-started/build-and-test.md), then explicitly install the xgen-quality commit pinned by `tools/quality.json`. The shared package owns tool versions; missing or mismatched tools fail.

```sh
cmake --preset gcc-test \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset gcc-test
python tools/quality.py cppcheck --build-dir build/gcc-test
python tools/quality.py tidy --build-dir build/gcc-test
```

`xgl_static_analysis` invokes the same shared `cppcheck` entry; run it with `cmake --build build/gcc-test --target xgl_static_analysis`. It uses the Python interpreter discovered at configure time, so configure within the environment containing the quality package or explicitly set `Python3_EXECUTABLE`. Run Clang-Tidy through the `tidy` command above.

Source development uses the same entries with its configured dev directory, for example `--build-dir build/dev-debug`. Analysis helpers default to disabled under a parent project; explicitly enable `XGL_BUILD_STATIC_ANALYSIS_TARGET` when needed. Production builds do not install quality tools.

## Inputs and diagnostics

The shared runner selects production sources declared in `tools/quality.json` from the real `compile_commands.json`, preserving compiler, macros, includes, and profile. Cppcheck uses the shared warning, performance, and portability policy. Clang-Tidy reads the root `.clang-tidy`, currently enabling `clang-analyzer-*`, `bugprone-*`, and `performance-*`. Do not replace the compilation database with an independently maintained include list.

The quality package controls fixed tools and optional local-path variables `XGEN_CPPCHECK` and `XGEN_CLANG_TIDY`. Selecting an executable still requires its version to match. Missing databases, empty production inputs, tool failures, and blocking diagnostics return nonzero. Results and selected sources are retained in `out/reports/`; local, CMake, and CI entries share these decisions.

Public API comments are checked separately with `python tools/quality.py docs`. The root `Doxyfile` makes missing documentation, parameters, and documentation errors fail; the site inherits that configuration to generate HTML. Formatting uses `python tools/quality.py format`, with its result reported separately from analysis.

## Limits

Analysis covers the compiled configuration. Record Full, Embedded, Boot, and compiler results separately; excluded features do not inherit a pass. Static analysis complements runtime, capacity, and wire tests. It does not establish constant-time cryptography, interrupt safety, DMA lifetime correctness, or worst-case call-stack bounds.
