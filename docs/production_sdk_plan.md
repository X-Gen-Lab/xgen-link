# Production SDK Plan

This plan defines the work needed to move xgen-link from a validated embedded protocol stack to a production-grade embedded SDK.

## Production Definition

xgen-link is production-grade when downstream firmware teams can:

- consume it through CMake package export without source-tree assumptions
- select a documented embedded profile for bare-metal, FreeRTOS, or host tests
- build with warnings-as-errors without third-party warning leakage
- understand all public API behavior, error semantics, ownership rules, and feature limits
- prove memory, stack, and flash budgets for a target profile
- run unit, integration, property, example, install, footprint, and static-analysis checks
- port PHY, time, mutex, atomic, and allocator services without touching protocol code
- audit codec, routing, zero-copy, reliability, and fragmentation behavior through tests

## P0: Contract And SDK Consumability

Goal: make public promises consistent and verifiable.

- Align README, API guide, and public header comments for zero-copy, compression, encryption, threading, and resource ownership.
- Keep `xgl_send_zerocopy` documented as true zero-copy only for single-frame unreliable TX; reliable TX may copy for retransmission.
- Add a resource model that identifies init-time allocation, runtime allocation, stack buffers, and no-heap profile gaps.
- Add an install-package consumer smoke test using `find_package(xgl CONFIG REQUIRED)`.
- Make SDK package exports exclude test-only dependencies and build-tree assumptions.

Exit criteria:

- `cmake --preset test`
- `cmake --build build/test`
- `ctest --preset test`
- install consumer smoke passes
- documentation has one consistent feature contract

## P1: Deterministic Embedded Profiles

Goal: make memory and scheduling behavior explicit enough for MCU integration.

- Add CMake options for `XGL_PROFILE_HOST`, `XGL_PROFILE_BARE_METAL`, and `XGL_PROFILE_FREERTOS`.
- Add a no-runtime-heap profile that rejects runtime allocation after `xgl_init`.
- Route datalink large-frame buffers, reliable queue nodes, ACK bitmap, route table, hash table, fragment buffers, and tiered pool storage through allocator or static profile storage.
- Add allocation-phase tracking: init, steady-state send, receive, retransmit, fragment reassembly.
- Add stack-usage guidance per compiler and example target.

Exit criteria:

- tiny and bare-metal profile tests prove no runtime heap on the acceptance path
- footprint report includes allocation phase counts and configured limits
- FreeRTOS example reports stack high-water mark hook points

## P2: Typed Layer Contracts

Goal: remove implicit cross-layer `void*` message contracts.

- Introduce `xgl_layer_message_t` with explicit variants for packet, frame TX, frame RX, and error.
- Replace anonymous cross-layer structs with named public or internal message types.
- Keep compatibility inside internal APIs; do not expand the application API unnecessarily.
- Add compile-time assertions for frame layout, message sizes, and alignment.

Exit criteria:

- layer interfaces no longer require callers to know anonymous struct layouts
- tests cover packet send, local receive, forwarding, and error propagation through typed messages

## P3: Reliability And Routing Hardening

Goal: make protocol behavior robust under loss, duplication, and multi-PHY forwarding.

- Add deterministic sequence-space tests around wraparound, duplicate ACK, stale ACK, and target-specific windows.
- Define whether reliable windows are global or per target; make API docs and implementation match.
- Add route metric semantics and route replacement rules.
- Add forwarding loop controls, TTL or hop budget if multi-hop is supported.
- Add stress tests for burst loss, reorder, duplicate frames, ACK loss, and mixed reliable/unreliable traffic.

Exit criteria:

- reliable semantics are specified in docs and covered by tests
- multi-PHY forwarding cannot loop indefinitely under supported topology rules

## P4: Codec And Security Boundary

Goal: keep optional advanced features composable and auditable.

- Keep compression and encryption as codec modules outside the base link path.
- Define codec ownership, in-place/out-of-place behavior, maximum expansion, error semantics, and sequencing relative to fragmentation.
- Add reference no-op, RLE, and test encryption codecs as examples only.
- Add negative tests for missing codec, expansion overflow, and decode failure.

Exit criteria:

- base SDK can build without codec feature code beyond the registry
- codecs have independent tests and documented resource budgets

## P5: Release Engineering

Goal: make releases repeatable for firmware teams.

- Add version policy and semantic compatibility rules.
- Add release package validation: install, import, build consumer, run examples, generate footprint, run static analysis.
- Add CI matrix for Windows/MSVC, Windows/Clang, Linux/GCC, Linux/Clang.
- Add generated Doxygen or equivalent API reference.
- Add changelog entries tied to conventional commits.

Exit criteria:

- release candidate can be validated from a clean checkout with documented commands
- SDK archive contains headers, library, CMake package files, examples, and docs
