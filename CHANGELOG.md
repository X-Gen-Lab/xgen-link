# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Opt-in `advanced_runtime` build capability with versioned options, independent message slots, scoped receive/event callbacks and allocation-free resource snapshots, without changing the existing configuration ABI.
- Resumable parser candidate budgets, rotating transport maintenance submission budgets, absolute reassembly and reliable-scope busy-period limits, with explicit pending and callback ownership contracts.
- Installed-scope authentication policy shared by TX, RX and zero-copy layout; closed scopes retain downgrade protection.
- Explicit host-test presets that reject empty test sets, pure C11 installed API consumers, and separately identified legacy/advanced Cortex-M0 probes.
- Error-returning checked instance reservation and side-effect-free deadline queries for a proposed work budget, preserving the legacy creation and timeout APIs.
- Public send/configuration consistency regressions and a four-combination API matrix for single-window and fixed-RTO builds.
- Explicit reliable-index bucket, compiled-window and adaptive-RTO selection, with exported target/header/runtime identity checks and fixed-RTO installed-consumer tests.
- Separate small-transport Cortex-M0 budgets bound to their actual compiled capabilities; keep the original default budgets and report their failures independently.
- Bounded RX chunk selection through CMake, with installed-header consistency and target-derived footprint budget identity.
- Checked borrowed SACK views and opt-in Cortex-M0 footprint LTO with separate mode-specific budgets.
- Scoped zero-copy sends and side-effect-free exact layout queries, while retaining the original zero-copy request ABI.
- Exact workspace reports by resource category, including alignment and fixed reservations.
- An Embedded Lite profile with fragmentation and out-of-order delivery, without authentication or forwarding; installed packages expose required capabilities.
- Capability-selected Boot GoogleTest coverage, multi-configuration build/install contracts, and pinned Cortex-M0 resource regression gates for MTU 128 and 64.
- Caller-owned static workspace with exact size/alignment queries and bounded resource accounting.
- Explicit monotonic-time send/step/deadline APIs and per-link receive budgets.
- Trusted directional authentication sessions, replay protection and nonce-domain retirement.
- Full, Embedded and Boot build profiles, installed C consumer checks and Cortex-M0 footprint probes.
- Explicit standalone `dev` assembly with fixed development dependency inputs.
- Individual GoogleTest discovery, preserved historical test inventory and reproducible property seeds.
- Shared text, formatting, test inventory, Cppcheck, clang-tidy, strict public API documentation and production coverage gates.

### Changed

- Arbitrate bounded retries and message tails by peer, enforce busy-period expiry before incoming DATA delivery, and preserve admitted out-of-order fragment deadlines across ordering and clock wrap.
- Reject incomplete or unbounded linked-function stack evidence at report generation, with invalid numeric maxima withheld; include development assembly and nested CMake files in the probe source digest.
- Clear reliable indexes once before bulk release, keeping owned-record and target-isolation behavior while avoiding collision-chain quadratic teardown.
- Distinguish enabled advanced capability from the API path exercised by a footprint probe; original Boot budgets explicitly reject advanced images without relaxing their limits.
- Bulk-load prevalidated routes, merge PHY frequencies before conversion, and reduce ACK/SACK and deadline arithmetic while preserving admission and retry semantics. All eight existing Boot budgets now pass without capability or limit changes.
- Run route contracts in every profile and cover overlapping reloads, high-frequency polling, cross-byte SACKs and acknowledgement overflow boundaries.
- Apply terminal-scope DATA admission to ordinary and caller-buffer sends and layout queries; enforce the active per-message limit across all public send paths.
- Validate optional authentication providers and mandatory local authenticated frame capacity before allocation; retain smaller transit routes and per-frame MTU checks.
- Ignore inactive fragmentation limits, retain capability-aware historical tests, and validate installed fixed-RTO CTest labels.
- Reuse exact peer lookup and scope admission without adding instance state; retain unchanged Cortex-M0 small-profile resource ceilings.
- Share deadline calculations while compiling the legacy query without work-budget dispatch.
- Validate complete ACK ranges before merging them with ordered outstanding records; process SACK against owned records and reuse known records during ACK removal and TX rollback.
- Keep RX declarations in a dedicated private header, reuse decoded fragment metadata and the out-of-order insertion position without adding permanent caches.
- Footprint format 6 records resolved transport capacity, buckets, adaptive RTO and resource identity; budget policies reject another compiled combination.
- Defer SACK retries and ACK-triggered message pumping to transport maintenance, with immediate deadlines for ready work and preserved local backpressure delays.
- Inline send-window bits, own out-of-order payload and TLVs in one block, and consolidate reassembly lengths; retain the public `window_bytes` report field as zero and count inline windows under `peer_bytes`.
- Bound ACK/SACK scratch to encoded sizes and retain only final send-plan results; footprint format 5 explicitly reports the removed window pool.
- Require validated views at internal frame RX endpoints; keep raw decoding separate with instance-size admission, and share PHY deduplication between workspace planning and link setup.
- Preserve original forwarding submission errors and remove redundant TX extension clearing without reducing supported extension capacity.
- Recover stream framing from retained bytes after malformed headers or CRC failures; consume one validated frame at a time and drain cached successors without new input. Private parser callers must advance after COMPLETE/ERROR; explicit reset still discards the cache.
- Boot now defaults to a 32-byte PHY read chunk; other profiles retain 128 bytes. Products may explicitly select 16/32/64/128 and must account for partial reads when driving steps.
- Datalink shares synchronous span consumption and direct wire status classification, with compact per-PHY parser state and four directional packet/frame endpoints.
- Reuse checked TX layouts during synchronous encoding; consolidate frame construction and remove unused private metadata fields.
- Decode directly into the borrowed frame view and calculate header CRC incrementally; preserve complete validation and failure-output contracts.
- Read SACK bitmaps without a transport stack copy, retaining the 64-byte acceptance limit.
- Remove redundant private link, packet and layer-context fields; security remains the sole owner of provider and authentication policy.
- Bound Boot logical TX metadata to 32 bytes without reducing the wire decoder's extension capacity; trim disabled statistics pointers from Boot contexts.
- Consolidate private transport TX declarations into one header and document retry, parser deadline and resource admission contracts.
- Consolidate transport lifecycle, TX, RX, ACK and maintenance into 17 implementation files, with private headers colocated under their owning layers.
- Share frame planning, encoding and PHY submission across copied, authenticated, in-place and forwarded traffic; propagate validated RX views without repeating full-frame CRC.
- Own security state once per instance and reserve exactly one parser and RX cache per unique PHY.
- Remove optional statistics and callback diagnostic text from Boot; retain error codes and report unsupported statistics explicitly.
- Organize tests by layer and check every public/private header independently in C11 and C++17.

- Consume independent status, bytes, CRC, memory and containers packages through compatible parent targets or installed packages.
- Use C11 production code and strict C++17 host tests; public configuration presets support both languages.
- Allocate reliable ACK state as a compact bitset and reuse bounded workspace across static and allocator-backed initialization.
- Instrument only protocol production objects for native GNU coverage, including Windows; reject unsupported requests explicitly.

### Fixed

- Preserve each reassembly's accepted reliability, reject mixed fragment attributes before feedback, and expire unreliable messages without terminating unrelated reliable history.
- Reserve local reliable resources before HELLO and reclaim unused remote HELLO reservations after five seconds even when ordinary idle expiry is disabled; duplicates do not renew the lease.
- Preserve failed-scope and owned-message error precedence under global capacity pressure, and retain accepted receive facts after ACK submission failure.
- Reject reserved packet types, reliable broadcasts, duplicate fragment metadata and exhausted reliable DATA packet numbers before receive admission.
- Reclaim only a newly admitted unused TX scope when its first send fails, preserving existing scope history and outstanding data.
- Return terminal retry, delivery and reassembly errors from maintenance while continuing other peers; preserve owned data across temporary backpressure.
- Report immediate ACK submission errors without rolling back accepted receive history or delivering accepted bytes twice.
- Distinguish retained application backpressure from failed feedback after buffered delivery; preserve hard errors over earlier temporary ACK failures within and across peers.
- Reject zero, local and duplicate static destinations before instance allocation; validate whole route sets before replacing existing entries.
- Count failed zero-copy logical submissions consistently with ordinary transport sends while keeping basic request rejection and layout queries free of TX counter changes.
- Correct the network property generator to exclude the local destination from valid random senders; add real reliable/authenticated multihop and reverse-ACK regressions.
- Restart the candidate deadline when overlapping magic replaces a partial header, and reject impossible frame lengths as soon as the fixed header is available.
- Keep the legacy parser view allocation off the production receive hot path; retain complete validation and update installed-SDK smoke scheduling for bounded reads.
- Reject overflowing frame-size calculations and invalid extension/tag budgets before arithmetic; bound payload budgets to wire limits.
- Keep the Boot update simulation valid when statistics are compiled out, while retaining dropped-ACK recovery and write-once checks.
- Bound successful SACK fast retransmission per pending record, preserve deferred retries after BUSY, and count successful retries before later failures.
- Reclaim a newly created RX peer after failed first admission and widen RTT intermediates before applying timeout bounds.
- Expire partial parsers independently of PHY polling and RX byte budgets; keep parser deadlines visible and process all already-read bytes while reporting the first receive error.
- Reject authenticated broadcast sessions and sends before consuming nonce state; count successful network sends consistently across copied, in-place and forwarded paths.
- Commit every valid SACK acknowledgment before attempting a fast retransmission that may return BUSY.
- Validate sends before peer admission, retain completed reassembly byte charges until release, and return reliable admission records without a second lookup.
- Reject incomplete CRC frames, malformed spans, overflowing lengths and invalid in-place capacity before pointer arithmetic or frame mutation.
- Report accepted ACK RTT samples and complete workspace reservations consistently across initialization and statistics reset.

- Reject oversized ACK range counts before length multiplication can overflow.
- Clear borrowed RX storage before failed-initialization cleanup can return it twice.
- Keep dependency includes outside C language linkage and compile headers independently as C++17.
- Classify tests without relying on inherited CTest policies and accept both valid MSVC C++17 flag spellings.
- Validate dependency identity, ABI, target kind and same-package version consistency before consumption.

### Removed

- Generic foundation implementations, old core dependency, nested production submodules and implicit source-directory dependency selection.
- Protocol-owned platform time, mutex and timer wrappers; callers provide time and serialized access.

## [1.0.0] - 2026-01-XX

### Added

#### Project Infrastructure
- Project directory structure for embedded protocol stack
- README.md with project overview and quick start guide
- .clang-format for consistent code formatting
- CMake build system with presets
- Kconfig configuration system

#### Core Protocol Stack
- Multi-instance architecture support
- Instance management (xgl_instance.c)
- Configuration system (xgl_config.c)
- Error handling (xgl_error.c)
- Statistics tracking (xgl_stats.c)
- CRC calculation (xgl_crc.c)
- Serialization utilities (xgl_serialize.c)

#### Data Structures
- Linked list implementation (xgl_list.c)
- Hash table implementation (xgl_hashtable.c)
- Memory pool management (xgl_mempool.c)
- Tiered memory pool (xgl_tiered_pool.c)
- Packet pool (xgl_packet_pool.c)
- Memory allocator abstraction (xgl_allocator.c)

#### Data Link Layer
- Frame structure and handling (xgl_frame.c)
- Protocol parser (xgl_parser.c)
- Data link layer implementation (xgl_datalink.c)

#### Network Layer
- Routing functionality (xgl_route.c)
- Network layer implementation (xgl_network.c)
- Sequence number management (xgl_sequence.c)

#### Transport Layer
- RTT (Round-Trip Time) estimation (xgl_rtt.c)
- Sliding window protocol (xgl_window.c)
- Reliable transmission (xgl_reliable.c)
- Acknowledgment handling (xgl_ack.c)
- Packet fragmentation (xgl_fragment.c)
- Transport layer implementation (xgl_transport.c)

#### Platform Abstraction
- Mutex abstraction (xgl_mutex.c)
- Time utilities (xgl_time.c)
- Platform interface (xgl_platform.c)

#### Test Framework
- Google Test integration
- Comprehensive unit tests for all modules:
  - Core: instance, config, CRC, serialization, statistics
  - Data structures: list, hash table, memory pools, allocator
  - Data link: frame, parser, datalink
  - Network: route, network, sequence
  - Transport: RTT, window, reliable, ACK, fragment, transport
  - Platform: mutex, time, platform
- Integration tests
- Property-based tests
- Mock implementations for testing

#### Examples
- Echo server example
- File transfer example
- Multi-node communication example

#### Documentation
- Architecture overview
- API reference documentation
- Porting guide
- Example documentation

#### Build System
- CMake configuration with modern practices
- CMakePresets.json for standardized builds
- Support for Debug and Release configurations
- Optional test building
- Optional example building
- Code coverage support

### Features

#### Zero-Copy Optimization
- Minimized memory bandwidth usage
- Reduced CPU overhead
- Efficient buffer management

#### Compile-Time Configuration
- Kconfig-based configuration
- Elimination of unused code
- Optimized binary size

#### Thread Safety
- Optional mutex protection
- RTOS environment support
- Safe multi-threaded operation

#### Bare-Metal Support
- No RTOS dependencies required
- Minimal resource footprint
- Suitable for resource-constrained systems

#### Resource Efficiency
- 32KB RAM for basic configuration
- 50KB Flash for basic configuration
- Configurable memory usage

### Technical Specifications
- C11 standard compliance
- C++20 for test code
- CMake 3.21+ build system
- Cross-platform support (Windows, Linux, macOS)
- Compiler support: GCC, Clang, MSVC
