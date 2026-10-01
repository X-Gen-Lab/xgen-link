# Validation matrix

## Protocol coverage

| Area | Evidence |
| --- | --- |
| Wire v3, TLV, CRC, unaligned spans | `test/test_wire.cpp`, `test/test_parser.cpp`, frame properties |
| Fixed routes and authenticated forwarding | `test/test_route.cpp`, `test/test_network.cpp` |
| Exact scope, atomic ACK, RX retention, retry, RESET and close | `test/test_transport.cpp`, transport properties |
| Fragment budgets, overlaps, timeout, messages larger than a window | `test/test_fragment.cpp`, memory properties |
| Explicit trusted sessions, nonce/AAD, replay, closed slots | `test/test_security.cpp` |
| Static authenticated lost-ACK recovery and exactly-once delivery | `test/test_send.cpp` |
| Static workspace, backpressure, wraparound, storage reuse | `tools/static_workspace_smoke.c` |
| One backend reservation and no runtime backend allocations | `test/test_footprint.cpp`, memory properties |
| Public installed C consumer, ABI/profile rejection | SDK consumer CTest and instance tests |

## Build coverage

The local Full matrix includes protocol regressions and four host examples. Boot and Embedded are independently built as C11 with libc disabled and tested through static-workspace and installed-package consumers. Independent foundation packages own their behavior, minimal-consumption, installation, and quality matrices; the protocol validates their joint integration. Source core tests are a migration baseline and do not qualify the new providers.

During the previous five-package migration, intermediate checkpoint `6927196` passed 4/4 CompactWindow; the effective RED is `b169a10`. That stage's final Full builds passed 8/8 CTest and 510/510 GoogleTest on GCC and MSVC. Embedded passed 4/4 CTest (510 GoogleTests), and Boot using the then-default five submodules passed 2/2 CTest. Memory and containers passed their GCC/MSVC and shared quality checks. Active core implementations and the old gitlink were retired. `REFACTORING_STATUS.md` preserves those historical revisions, coverage, negative consumers, and resource figures.

The current change removes link's foundation submodules and production source paths. Production consumption uses provided targets or installed packages; source development uses the separate dev harness. This round passed 10/10 production-contract checks and dev CTest matrices of Full 8/8, Embedded 4/4, and Boot 2/2; Full and Embedded each executed 510 GoogleTests. The ARM rebuild with explicit dev sources produced the same resource figures as the previous round. These are current local results; MSVC and Linux sanitizers were not rerun, and remote or hardware validation was not executed. Detailed paths are in the implementation record.

Removed generic utility and platform tests live in the owning layer or are no longer applicable; a lower protocol test count is not presented as an identical historical suite.

## Resource and product limits

The Cortex-M0 probe records a real linked ELF, map, target-ABI workspace formula and stack-use files. Its results are reproducible from `tools/boot_footprint/README.md`. Native execution separately checks the layout formula against actual workspace initialization.

No real board, Flash power interruption, ISR nesting, full call-chain stack bound, DMA lease, production cipher provider or cross-restart freshness has been qualified. Boot update uses a host Flash model and CRC16 for accidental corruption only. Linux sanitizer CI is configured; do not infer it ran from Windows results.
