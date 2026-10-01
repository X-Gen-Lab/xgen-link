# Validation matrix

## Protocol coverage

| Area | Evidence |
| --- | --- |
| Wire v3, TLV, CRC, unaligned spans | `test/unit/wire/test_wire.cpp`, `test/unit/datalink/test_parser.cpp`, frame properties |
| Fixed routes and authenticated forwarding | `test/unit/network/test_route.cpp`, `test/unit/network/test_network.cpp` |
| Exact scope, atomic ACK, RX retention, retry, RESET and close | `test/unit/transport/test_transport.cpp`, transport properties |
| Fragment budgets, overlaps, timeout, messages larger than a window | `test/unit/transport/test_fragment.cpp`, memory properties |
| Explicit trusted sessions, nonce/AAD, replay, closed slots | `test/unit/security/test_security.cpp` |
| Static authenticated lost-ACK recovery and exactly-once delivery | `test/unit/api/test_send.cpp` |
| Static workspace, backpressure, wraparound, storage reuse | `tools/static_workspace_smoke.c` |
| One backend reservation and no runtime backend allocations | `test/unit/api/test_footprint.cpp`, memory properties |
| Public installed C consumer, ABI/profile rejection | SDK consumer CTest and instance tests |
| ACK size overflow, failed-init release, PHY failure and recovery | `test/unit/wire/test_coverage_wire.cpp`, `test/unit/api/test_coverage_api.cpp`, `test/integration/test_coverage_delivery.cpp` |
| RTT observations, workspace reservations and streaming CRC classification | `test/unit/api/test_stats.cpp`, `test/unit/datalink/test_datalink.cpp` |
| Boot error contracts with statistics and detailed diagnostics disabled | Conditional checks in `tools/static_workspace_smoke.c` |
| Independent C11/C++17 public/private header compilation, discovery and seed replay | `test/cmake/HeaderContracts.cmake`, classification and runner contracts |

## Build coverage

The local Full matrix includes protocol regressions and four host examples. Boot and Embedded are independently built as C11 with libc disabled and tested through static-workspace and installed-package consumers. Independent foundation packages own their behavior, minimal-consumption, installation, and quality matrices; the protocol validates their joint integration. Source core tests are a migration baseline and do not qualify the new providers.

During the previous five-package migration, intermediate checkpoint `6927196` passed 4/4 CompactWindow; the effective RED is `b169a10`. That stage's final Full builds passed 8/8 CTest and 510/510 GoogleTest on GCC and MSVC. Embedded passed 4/4 CTest (510 GoogleTests), and Boot using the then-default five submodules passed 2/2 CTest. Memory and containers passed their GCC/MSVC and shared quality checks. Active core implementations and the old gitlink were retired. `REFACTORING_STATUS.md` preserves those historical revisions, coverage, negative consumers, and resource figures.

Production consumption uses provided targets or installed packages; source development uses the separate dev harness. Development dependencies and shared quality tools pin published commits. In the preceding independent-delivery stage, fresh remote clones ran 561 checks for GNU Full and Embedded (554 GoogleTests and seven smoke/examples), and two for Boot, including installed consumption in every profile. Those results qualify that earlier stage, not subsequent source changes.

For this protocol-structure refactor, local GNU Full/Embedded and MSVC Full each run 582 checks (575 GoogleTests and seven smoke/examples); GNU Boot runs two. Independent header checks compile four public headers, 24 private headers and the generated configuration header first in both C11 and C++17. Root `REFACTORING_STATUS.md` records the latest sanitizer, coverage, ELF, source revisions and remote execution status.

Production dependency contracts now contain 11 cases, classification has an independent CTest-policy regression, and all public, private and generated configuration headers compile as first includes in separate C11 and C++17 translation units. The version-controlled inventory retains every one of the original 510 protocol names. Generic component tests moved to their owning repositories in an earlier stage and are distinct from this inventory. Production lines, functions and branches each have a coverage gate; reports preserve exact counts, tools and source identities.

[Remote CI](https://github.com/X-Gen-Lab/xgen-link/actions/runs/36807629994) passed on clean revision `1b0ce7e`: 561 checks each on Windows/MSVC, Linux and macOS, 560 with ASan/UBSan, and 2/3 for CI Boot/Embedded. Production coverage is 3671/4010 lines (91.5%), 249/250 functions (99.6%) and 2314/2865 branches (80.8%). Cppcheck, Clang-Tidy, strict documentation, installed consumption and release validation passed. All eight artifacts match their GitHub SHA256 digests and report the same clean source identity.

## Resource and product limits

The Cortex-M0 probe records a real linked ELF, map, target-ABI workspace formula and stack-use files. Its results are reproducible from `tools/boot_footprint/README.md`. Native execution separately checks the layout formula against actual workspace initialization.

No real board, Flash power interruption, ISR nesting, full call-chain stack bound, DMA lease, production cipher provider or cross-restart freshness has been qualified. Boot update uses a host Flash model and CRC16 for accidental corruption only. Actual Linux sanitizer executions are recorded separately; do not infer them from Windows results.
