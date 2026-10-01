# Testing strategy

## Ownership

Each independent foundation repository owns its behavior: allocators/pools in xgen-memory, containers in xgen-containers, checksums in xgen-crc, byte access in xgen-bytes, and generic statuses in xgen-status. Protocol tests exercise wire encoding, layer boundaries, routing, peer state, fragmentation, security and public instance behavior. Removing generic wrappers also removes their duplicate protocol tests; real integration with those services still requires protocol validation.

## Regression cases

Use real protocol transitions for ACK atomicity, retained RX ownership, backpressure, retry and explicit scope close. Capacity tests cover exact workspace measurement, undersized/alignment rejection, one backend reservation, runtime reuse and destruction. Static authenticated endpoints verify fresh security sequence on retransmission and exactly-once application delivery.

## Running

Use CTest for protocol tests, host examples, static lifecycle and installed C consumer checks. Configure the Boot and Embedded profiles separately. See the [validation matrix](../reference/validation-matrix.md). Randomized property tests complement fixed regressions; they are not a substitute for sanitizer, fuzz or hardware qualification.

Use root presets after installing dependencies, or use `cmake -S dev` with all five `XGL_DEV_*_SOURCE_DIR` paths for source development as described in [Build and test](../getting-started/build-and-test.md). CTest runs from the corresponding `build/dev-<profile>` root. Product subdirectory consumption does not add examples, smoke tests, or release helpers by default. Record new-entry results separately from previous stages.

## Style

Follow the repository clang-format and Doxygen format. New tests should demonstrate an invariant or a regression, not duplicate implementation code. Tests must preserve the borrowed configuration/PHY lifetimes and pass explicit time.

Blank-line layout follows engineering rules C-020, C-021 and DOC-013: separate independent definitions and public API documentation groups with one blank line, keep Doxygen adjacent to its declaration, and group function statements by meaning. clang-format 19.1.5 handles definition separation and excess blank lines. The shared xgen-quality `format` entry adds bounded structural checks for common public C headers; it does not infer business phases inside function bodies.

Explicitly install xgen-quality 0.1.0 from the revision pinned in `tools/quality.json`, then run:

```sh
python tools/quality.py format
python tools/quality.py text
python tools/quality.py test --build-dir build/dev-full
python tools/quality.py cppcheck --build-dir build/dev-full
python tools/quality.py tidy --build-dir build/dev-full
python tools/quality.py docs
python -m pre_commit run --all-files
```

Checks are read-only. Apply clang-format 19.1.5 with `-i` explicitly. Analysis uses the real toolchain compilation database. CI defaults to official repositories, accepts repository-variable overrides, and pins full source revisions. Root `REFACTORING_STATUS.md` records actual local and remote evidence separately.

## TDD and test inventory

For behavior changes, reproduce the requirement or defect with a failing test, record RED, implement the smallest correction, verify GREEN, then refactor and rerun relevant regressions. Do not manufacture failures for existing-behavior coverage, documentation, or formatting changes.

Host tests use GoogleTest/GoogleMock 1.16.0 and strict C++17. `gtest_discover_tests` registers individual cases; classification assigns `xgl` and `unit` or `integration`, with an additional `property` label for randomized properties. Examples, static lifecycle and installed consumption are integration tests. `test/cmake/baseline_tests.txt` retains all 510 original names; discovery checks every name rather than trusting only a growing count. The shared runner rejects empty, disabled, skipped, duplicate or failed cases.

## Property replay

Seed precedence is `--xgl_property_seed=N`, then `XGL_PROPERTY_SEED`, then the fixed default `5785420`. Invalid or out-of-range unsigned values are rejected. Each test derives its own stable stream, so filtering does not change its input. JUnit records both the base seed and test stream. Failures print a replay command:

```sh
./build/dev-full/link/test/xgl_tests --gtest_filter='XglFrameProperties.*' --xgl_property_seed=5785420
ctest --test-dir build/dev-full -L property --output-on-failure
```

Use `.exe` on Windows and the actual configuration subdirectory for multi-config builds.

## Coverage and evidence

Configure a separate native GNU build with `-DXGL_ENABLE_COVERAGE=ON`. Only the production `xgl` target is instrumented; dependencies, GoogleTest and test sources are excluded from protocol metrics. Start with fresh counters or a new build after source/configuration changes, then run all tests:

```sh
python tools/quality.py test --build-dir build/coverage
python tools/quality.py coverage --build-dir build/coverage --gcov-executable gcov
```

Lines, functions and branches must each reach 80%; neither averaging nor excluding difficult production files is acceptable. Unsupported coverage toolchains fail configuration explicitly. Reports are written to `out/reports`. Host, installed consumption, sanitizer, ARM ELF and hardware results remain separate.
