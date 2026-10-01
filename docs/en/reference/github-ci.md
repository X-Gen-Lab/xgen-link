# GitHub CI

## Fixed inputs and triggers

`.github/workflows/ci.yml` runs for pushes to `main`, `develop`, and `feat/**`, pull requests targeting `main` or `develop`, and manual dispatch. The final `status` job requires every selected job to succeed; failures, cancellations, and unexpected skips fail the gate. Pages accepts pushes to `main` or manual dispatch only. Feature-branch CI does not automatically deploy the site.

CI reads fixed status, bytes, CRC, memory, and containers commits from `dev/dependencies.json`. `setup-components` explicitly checks out and verifies these sources, then supplies five `XGL_DEV_*_SOURCE_DIR` values to `dev`. Acquisition is separate from CMake configuration. Production consumes parent-provided targets or installed packages; each product still selects one component combination per image.

`tools/quality.json` is the sole declaration of the shared quality source. `setup-quality` installs that commit; Linux analysis additionally builds fixed Cppcheck and Doxygen commits. `setup-gtest` prepares GoogleTest/GoogleMock 1.16.0 with the same native compiler as the host build. Configuration never downloads or selects a latest version.

| Optional repository variable | Address when unset or empty |
| --- | --- |
| `XGEN_QUALITY_REPOSITORY` | `X-Gen-Lab/xgen-quality` |
| `XGEN_STATUS_REPOSITORY` | `X-Gen-Lab/xgen-status` |
| `XGEN_BYTES_REPOSITORY` | `X-Gen-Lab/xgen-bytes` |
| `XGEN_CRC_REPOSITORY` | `X-Gen-Lab/xgen-crc` |
| `XGEN_MEMORY_REPOSITORY` | `X-Gen-Lab/xgen-memory` |
| `XGEN_CONTAINERS_REPOSITORY` | `X-Gen-Lab/xgen-containers` |

Variables override `owner/repository`, not the pinned commit. Private forks additionally require checkout credentials with read access.

## Check matrix

| Job | Environment and checks |
| --- | --- |
| `host` | Ubuntu 24.04, Windows 2022/MSVC, macOS 15; text, format, pre-commit, C11 protocol, strict C++17 GoogleTest, examples, SDK consumer, discovery and seed-replay contracts |
| `analysis` | Ubuntu 24.04; native GNU instrumented build, shared test runner, Cppcheck, Clang-Tidy, strict public API Doxygen, independent 80% line/function/branch gates, release helpers and documentation site |
| `sanitizers` | Ubuntu 24.04; ASan/UBSan, building every selected target before running the shared test entry |
| `bounded-profiles` | Boot/Lite capability GoogleTest and complete Embedded regression; separate C11 builds without protocol libc fallback, static lifecycle and installation consumption |
| `dependency-contracts` | dev preparation, real target reuse, installation and dependency rejection; capability declarations, Debug/Release publication and budget-checker contracts |
| `arm-footprint` | Fixed Cortex-M0 GCC 15.2.1; eight default/small, MTU/RX 128/64 and LTO OFF/ON combinations, exact resource identity, heap/unresolved-symbol rejection and independent budgets |
| `status` | Require success from every mandatory job |

Host uses `cmake -S dev --preset debug` and writes to `build/dev-debug`. Analysis, sanitizers, and bounded profiles use `dev-ci`, `dev-asan`, `dev-boot`, `dev-embedded-lite`, and `dev-embedded`. They do not share ABI/profile outputs or coverage counters. The source-development CTest root is `build/dev-<preset>`.

GoogleTest registers individual cases with `unit`, `integration`, and `property` labels. Full/Embedded discovery preserves all 510 historical names; Boot/Lite check their required capability lists. The shared runner verifies selected names against actual JUnit results and rejects empty, disabled, skipped, or failing tests. See [testing](../contributing/testing.md) for commands and replay.

The ASan preset disables installed-package smoke because global sanitizer flags are not automatically exported as external consumer link requirements. Full and independent consumer contracts validate installation. Bounded CI configurations explicitly set `XGL_BUILD_TESTS=ON`; these host tests do not become MCU production dependencies.

The ARM preparation step reads `tools/boot_footprint/toolchain-lock.json`, downloads the official Arm 15.2.rel1 Linux archive, and verifies the pinned upstream SHA-256. CMake does not download a compiler. Default resources explicitly select 128 slots, one bucket and adaptive RTO; small selects one slot, one bucket and fixed RTO. They use `budget-mtu*.json` and `budget-small-mtu*.json` respectively, with `-lto` suffixes for LTO. Compiler, profile, MTU/RX, LTO and resolved resource identity must match. Missing required measurements or any exceeded limit fail.

The 2026-10-02 local default LTO measurement is 12,964 B, exceeding the original 12,800 B ceiling; small LTO is 12,788 B. The default jobs and original ceiling remain required, without matrix exclusions or ignored failures. This update does not claim remote CI success; evidence and unmet goals are recorded in the root `REFACTORING_STATUS.md`, with methods in [release validation](release-validation.md). Development/installed-consumer contracts also check buckets, windows, fixed-RTO behavior and header/target/runtime identity conflicts, with separate reports.

## Artifacts and conclusions

Jobs use `always()` to preserve applicable `out/reports/`, CTest logs, compilation databases, Doxygen diagnostics, and footprint reports. ARM additionally retains ELF, map, `.su`, layout, and budget JSON. Reports identify tools, source, configuration, selected tests, and actual exit status. Later failures preserve earlier evidence; missing required artifacts fail.

Local checks, remote runs, and product acceptance are recorded separately in the root `REFACTORING_STATUS.md`. A workflow definition does not establish that a commit passed. Products provide board execution, production cryptographic providers, power-loss recovery, and final MCU partition qualification; see [release validation](release-validation.md).
