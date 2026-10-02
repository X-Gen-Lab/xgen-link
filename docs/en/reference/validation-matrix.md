# Validation matrix

## Protocol coverage

| Area | Evidence |
| --- | --- |
| Wire v3, TLV, CRC, unaligned spans | `test/unit/wire/test_wire.cpp`, `test/unit/datalink/test_parser.cpp`, frame properties |
| Length overflow, borrowed SACK, failure-view clearing, and raw/stream agreement | `test/unit/wire/test_frame.cpp`, `test_wire_sack_view.cpp`, `test_wire_rx_contracts.cpp`, `test/unit/transport/test_transport_sack_view.cpp` |
| Fixed routes and authenticated forwarding | `test/unit/network/test_route.cpp`, `test/unit/network/test_network.cpp` |
| Exact scope, atomic ACK, RX retention, retry, RESET and close | `test/unit/transport/test_transport.cpp`, transport properties |
| Side-effect-free RX/TX rejection, deferred fast retry, terminal errors and unique storage ownership | `test_transport_rx_contracts.cpp`, `test_transport_tx_contracts.cpp`, `test_transport_runtime_contracts.cpp`, `test_transport_storage.cpp` (all under `test/unit/transport/`) |
| Fragment budgets, overlaps, timeout, messages larger than a window | `test/unit/transport/test_fragment.cpp`, memory properties |
| Explicit trusted sessions, nonce/AAD, replay, closed slots | `test/unit/security/test_security.cpp` |
| Static authenticated lost-ACK recovery and exactly-once delivery | `test/unit/api/test_send.cpp` |
| Static workspace, backpressure, wraparound, storage reuse | `tools/static_workspace_smoke.c` |
| One backend reservation and no runtime backend allocations | `test/unit/api/test_footprint.cpp`, memory properties |
| Public installed C consumer, ABI/profile rejection | SDK consumer CTest and instance tests |
| Exact creation errors, pure work-budget deadlines and clock wraparound | `test/unit/api/test_api_contracts.cpp` |
| Authentication preflight, base ACK capacity and smaller transit routes | `test/unit/api/test_config_consistency.cpp` |
| Message limits, terminal scope rejection and pure layout across send APIs | `test/unit/api/test_send_policy.cpp` |
| ACK size overflow, failed-init release, PHY failure and recovery | `test/unit/wire/test_coverage_wire.cpp`, `test/unit/api/test_coverage_api.cpp`, `test/integration/test_coverage_delivery.cpp` |
| RTT observations, workspace reservations and streaming CRC classification | `test/unit/api/test_stats.cpp`, `test/unit/datalink/test_datalink.cpp` |
| Boot error contracts with statistics and detailed diagnostics disabled | Conditional checks in `tools/static_workspace_smoke.c` |
| Boot/Lite reliable delivery, capacity rejection, parsing, and multi-window fragmentation | Capability-selected cases in `test/unit/api/test_bounded_profile.cpp` |
| Boot 32-byte logical-TX extension limit and independent raw-wire boundary | `test/unit/network/test_boot_network.cpp` |
| Multi-configuration publication/installation, exported capabilities, and budget rejection | `test/cmake/test_profile_builds.py`, `test/cmake/test_footprint_budget.py` |
| Four resource-configured API sets, installed fixed-RTO tests and complete labels | `test/cmake/test_transport_resource_config.py` |
| Independent C11/C++17 public/private header compilation, discovery and seed replay | `test/cmake/HeaderContracts.cmake`, classification and runner contracts |

## Build coverage

The latest API stage passed 808 checks each for local GNU Full/Embedded and MSVC Full, 188 for Boot, and 279 for Lite. Full protocol sets contain 801/805 cases with adaptive/fixed RTO; four resource configurations execute 196 selected API cases, and seven build/install contracts pass. All 510 historical names remain. See repository-root `design/validation/api-refinement.md` for environments, source identities and quality evidence; the capacity-one API selection does not qualify all private transport tests.

The local Full matrix includes protocol regressions and four host examples. Boot, Embedded Lite, and Embedded build separately as C11 without protocol libc fallback and check static-workspace and installed-package consumers. Explicitly enabled GoogleTest runs capability-selected Boot/Lite tests and complete historical Embedded regression. Independent foundation packages own their behavior, minimal-consumption, installation, and quality matrices; the protocol validates their joint integration. Source core tests are a migration baseline and do not qualify the new providers.

During the previous five-package migration, intermediate checkpoint `6927196` passed 4/4 CompactWindow; the effective RED is `b169a10`. That stage's final Full builds passed 8/8 CTest and 510/510 GoogleTest on GCC and MSVC. Embedded passed 4/4 CTest (510 GoogleTests), and Boot using the then-default five submodules passed 2/2 CTest. Memory and containers passed their GCC/MSVC and shared quality checks. Active core implementations and the old gitlink were retired. `REFACTORING_STATUS.md` preserves those historical revisions, coverage, negative consumers, and resource figures.

Production consumption uses provided targets or installed packages; source development uses the separate dev harness. Development dependencies and shared quality tools pin published commits. In the preceding independent-delivery stage, fresh remote clones ran 561 checks for GNU Full and Embedded (554 GoogleTests and seven smoke/examples), and two for Boot, including installed consumption in every profile. Those results qualify that earlier stage, not subsequent source changes.

In the preceding protocol-structure refactor, local GNU Full/Embedded and MSVC Full each ran 582 checks (575 GoogleTests and seven smoke/examples); GNU Boot ran two. Header checks then compiled four public headers, 24 private headers and the generated configuration header first in both C11 and C++17. Root `REFACTORING_STATUS.md` records the latest sanitizer, coverage, ELF, source revisions and remote execution status.

Production dependency contracts now contain 11 cases, classification has an independent CTest-policy regression, and all public, private and generated configuration headers compile as first includes in separate C11 and C++17 translation units. The version-controlled inventory retains every one of the original 510 protocol names. Generic component tests moved to their owning repositories in an earlier stage and are distinct from this inventory. Production lines, functions and branches each have a coverage gate; reports preserve exact counts, tools and source identities.

[Remote CI](https://github.com/X-Gen-Lab/xgen-link/actions/runs/36807629994) passed on clean revision `1b0ce7e`: 561 checks each on Windows/MSVC, Linux and macOS, 560 with ASan/UBSan, and 2/3 for CI Boot/Embedded. Production coverage is 3671/4010 lines (91.5%), 249/250 functions (99.6%) and 2314/2865 branches (80.8%). Cppcheck, Clang-Tidy, strict documentation, installed consumption and release validation passed. All eight artifacts match their GitHub SHA256 digests and report the same clean source identity.

## Resource and product limits

The latest API stage uses 16,348/12,964 B Flash without/with LTO for the default configuration, and 16,116/12,788 B for small (window one, one bucket, fixed RTO). Small MTU128/64 workspaces use 1,040/848 B; RAM with a 1 KiB stack reservation uses 2,128/1,936 B. All four small and both default non-LTO budgets pass; default LTO retains the preceding 164 B excess without relaxed limits. Older figures below retain their historical scope and do not replace current reports.

The Cortex-M0 probe records a real linked ELF, map, target-ABI workspace formula and stack-use files. Its results are reproducible from `tools/boot_footprint/README.md`. Native execution separately checks the layout formula against actual workspace initialization.

After the wire optimization, the fixed GCC 15.2.1 MTU/RX 128 and 64 final ELFs both use 15,932 B Flash, 128 B below the preceding 16,060 B baseline. Explicit LTO produces 12,600 B. Workspaces remain 1,136/944 B; RAM including the reserved 1,024 B stack remains 2,224/2,032 B. The largest single-function stack is 272 B without LTO and 312 B with LTO. All four combinations have no heap or unresolved symbols and pass separate budget policies. The normal 16 KiB and LTO 12,800 B ceilings are regression limits; the 8 KiB Flash design goal remains unmet. See [Wire optimization](wire-optimization.md) for comparisons, tradeoffs, and reproduction.

The CI matrix includes Boot/Lite capability tests, complete Embedded regression, multi-configuration installed consumers, and four ARM budget gates for MTU128/64 × LTO OFF/ON. Local script passes do not mean this new matrix has executed remotely; use actual results for the current commit.

The wire figures above are historical. After transport optimization, local final ELFs use 16,116/12,792 B Flash without/with LTO, workspaces of 1,064/872 B for MTU128/64, and RAM of 2,152/1,960 B including a 1 KiB stack reservation. The known ordinary-ACK call chain from step uses 988/844 B, excluding callers, real drivers and ISRs; reserved stack is not proof of product peak usage. See repository-root `design/validation/transport-optimization.md` for TDD, build matrices, coverage, resource tradeoffs and unexecuted qualification.

No real board, Flash power interruption, ISR nesting, full call-chain stack bound, DMA lease, production cipher provider or cross-restart freshness has been qualified. Boot update uses a host Flash model and CRC16 for accidental corruption only. Actual Linux sanitizer executions are recorded separately; do not infer them from Windows results.
