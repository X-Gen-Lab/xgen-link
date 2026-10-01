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
| `bounded-profiles` | Separate C11 Boot and Embedded builds without libc fallback, static lifecycle and installation consumption |
| `dependency-contracts` | dev preparation unit tests, real parent-target reuse, installation consumption, and rejection of missing or incompatible dependencies |
| `status` | Require success from every mandatory job |

Host uses `cmake -S dev --preset debug` and writes to `build/dev-debug`. Analysis, sanitizers, and bounded profiles use `dev-ci`, `dev-asan`, `dev-boot`, and `dev-embedded`. They do not share ABI/profile outputs or coverage counters. The source-development CTest root is `build/dev-<preset>`.

GoogleTest registers individual cases with `unit`, `integration`, and `property` labels. Discovery checks that all 510 historical names remain present. The shared runner verifies selected names against actual JUnit results and rejects empty, disabled, skipped, or failing tests. See [testing](../contributing/testing.md) for commands and replay.

The ASan preset disables installed-package smoke because global sanitizer flags are not automatically exported as external consumer link requirements. Full and independent consumer contracts validate installation. Boot/Embedded CI jobs do not execute GoogleTest; Host jobs provide protocol regression coverage.

## Artifacts and conclusions

Jobs use `always()` to preserve applicable `out/reports/`, CTest logs, compilation databases, Doxygen diagnostics, and footprint reports. Reports identify tools, source, configuration, selected tests, and actual exit status. Later failures preserve earlier evidence; missing required artifacts fail.

Local checks, remote runs, and product acceptance are recorded separately in the root `REFACTORING_STATUS.md`. A workflow definition does not establish that a commit passed. Products provide board execution, production cryptographic providers, power-loss recovery, and final MCU partition qualification; see [release validation](release-validation.md).
