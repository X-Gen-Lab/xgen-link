# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

#### Core Architecture
- Layer interface abstraction for modular protocol stack composition
- Time provider interface for platform-independent timing operations
- Configuration header (xgl_config.h) for compile-time settings
- Enhanced error code system with detailed error types
- Layer type enumeration for protocol stack layers

#### Platform Abstraction
- Platform-specific time provider implementation
- Improved platform abstraction layer

#### API Enhancements
- Allocator API with alignment support and statistics tracking
- Timeout control and retransmission management in transport layer
- Flow control and congestion management features
- Enhanced fragmentation with improved reassembly logic
- Improved acknowledgment handling and tracking

#### Testing
- New property-based test suites:
  - Alignment property tests for memory operations
  - Error handling property tests
  - Fragment property tests for packet fragmentation
  - Instance property tests for lifecycle management
- Expanded property-based testing coverage:
  - Comprehensive frame property tests
  - Memory allocation property tests
  - Network layer property tests with routing scenarios
  - Significantly expanded transport layer property tests
- New unit test suites:
  - Layered statistics tests for per-layer metrics
  - Time provider tests for platform abstraction
  - Timeout control tests for retransmission logic
- Enhanced integration tests with end-to-end scenarios

#### Documentation
- README files for all example applications:
  - Echo server documentation with usage instructions
  - File transfer example documentation
  - Multi-node example documentation
- Build and run instructions for examples
- Troubleshooting and configuration guides

#### Development Tools
- VSCode workspace settings for consistent development environment

### Changed

#### Core Implementation
- Refactored instance management with layer interface support
- Improved send path with better error handling
- Enhanced configuration system for new layer architecture
- Optimized memory allocation patterns

#### Protocol Layers
- Standardized function signatures across all layers (datalink, network, transport)
- Enhanced frame processing with improved validation
- Improved routing table management
- Optimized packet processing flow
- Enhanced error recovery mechanisms in transport layer

#### Examples
- Improved echo server with better error handling
- Enhanced file transfer with progress tracking
- Added advanced scenarios to multi-node example
- Updated examples to use new API features
- Improved user interface and logging

#### Testing
- Updated all unit tests to match new API signatures
- Enhanced mock implementations:
  - Mock allocator with alignment and statistics support
  - Mock callbacks with layer interface compatibility
  - Mock PHY with realistic behavior simulation
- Improved test coverage for error conditions
- Enhanced assertions and validation logic

#### Build System
- Updated CMake configuration for new source files
- Improved test build configuration
- Updated example build settings
- Better dependency management

### Removed
- Obsolete workspace configuration file (moved to .vscode directory)

---

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

