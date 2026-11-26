# Validation

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

## Build and installation

Use a clean build with only the C11 toolchain to validate the default production library, Boot production library, source subdirectory and isolated installed consumer. Compile, link and run real public C consumers. Exercise rejected ABI, profile and required capability combinations. Follow the [development workflow](workflow.md) for host tests. Keep missing test inventories, missing reports and failing exit statuses visible.

After preparing the release tools, configure the root `ci` preset and run `cmake --build build/ci --target xgl_release_validation`. The quality gate requires at least 80% each for line, function and branch coverage. Record which profiles, compiler and sanitizer runs actually executed. Tool and framework versions are defined by `tools/quality.json` and `test/CMakeLists.txt`.

## Resources and product qualification

Reproduce the Cortex-M0 probe using `tools/boot_footprint/README.md`. Compare target ELFs built with the same toolchain, profile, resource settings, MTU, RX and LTO selections. Record Flash, workspace, static RAM, reserved stack and maximum single-function stack. Qualify the complete board call chain, drivers, ISR nesting and measured stack peak separately.

Product qualification covers the actual board, Flash power interruption, DMA borrows, production authentication and nonce freshness after restart. Bind simulation and software checks, ARM linkage, remote CI, release and hardware execution to their actual inputs and artifacts.

## Historical evidence

The SDK 3 [implementation record](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/REFACTORING_STATUS.md), [refactoring plan](https://github.com/X-Gen-Lab/xgen-link/blob/f09efca25abe84c344ce8c41d5645006c7b61df9/REFACTORING_PLAN.md), [architecture decisions](https://github.com/X-Gen-Lab/xgen-link/tree/f09efca25abe84c344ce8c41d5645006c7b61df9/design/adr) and [validation records](https://github.com/X-Gen-Lab/xgen-link/tree/f09efca25abe84c344ce8c41d5645006c7b61df9/design/validation) remain available at the pinned revision. Consult the actual tested source identity and build configuration when reading historical results.
