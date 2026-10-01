# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Caller-owned static workspace with exact size/alignment queries and bounded resource accounting.
- Explicit monotonic-time send/step/deadline APIs and per-link receive budgets.
- Trusted directional authentication sessions, replay protection and nonce-domain retirement.
- Full, Embedded and Boot build profiles, installed C consumer checks and Cortex-M0 footprint probes.
- Explicit standalone `dev` assembly with fixed development dependency inputs.
- Individual GoogleTest discovery, preserved historical test inventory and reproducible property seeds.
- Shared text, formatting, test inventory, Cppcheck, clang-tidy, strict public API documentation and production coverage gates.

### Changed

- Consume independent status, bytes, CRC, memory and containers packages through compatible parent targets or installed packages.
- Use C11 production code and strict C++17 host tests; public configuration presets support both languages.
- Allocate reliable ACK state as a compact bitset and reuse bounded workspace across static and allocator-backed initialization.
- Instrument only protocol production objects for native GNU coverage, including Windows; reject unsupported requests explicitly.

### Fixed

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
