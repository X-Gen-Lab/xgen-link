# Implementation Plan: x_gen_link Protocol

## Overview

This implementation plan breaks down the x_gen_link protocol into discrete, actionable tasks. Each task focuses on writing, modifying, or testing code. Tasks are organized by component and include checkpoints for validation.

## Tasks

- [x] 1. Setup project structure and build system
  - Create directory structure (include/, src/, test/, examples/, docs/)
  - Create CMakeLists.txt with C11 standard and warning flags
  - Create CMakePresets.json with debug/release/test configurations
  - Create Kconfig file with compile-time configuration options
  - Create .clang-format file following Nexus standards
  - Setup Google Test and Google Mock integration in CMake
  - _Requirements: 31.1, 31.2, 31.3, 31.4, 33.1, 33.2, 38.1, 19.1_

- [x] 2. Implement core error handling
  - Create include/xgl/xgl_error.h with all error codes
  - Implement xgl_error_string() function
  - Add error code documentation
  - _Requirements: 8.1_

- [x] 3. Define core data types and structures
  - Create include/xgl/xgl_types.h with basic types
  - Define xgl_handle_t (opaque pointer)
  - Define frame header structure
  - Define packet structure with reference counting
  - Define configuration structures
  - Define callback function types
  - Define statistics structure
  - _Requirements: 1.1, 2.1, 10.1, 11.1_

- [x] 4. Implement CRC calculation module
  - Create src/core/xgl_crc.c and xgl_crc.h
  - Implement CRC8 (MAXIM polynomial) with lookup table
  - Implement CRC16 (MODBUS polynomial) with lookup table
  - _Requirements: 13.1, 13.2, 13.5_

- [ ]* 4.1 Write property test for CRC calculation
  - **Property 4: CRC Calculation Correctness**
  - **Validates: Requirements 13.1, 13.2, 13.3**


- [x] 5. Implement serialization utilities
  - Create src/core/xgl_serialize.c and xgl_serialize.h
  - Implement little-endian serialization for uint16_t and uint32_t
  - Implement little-endian deserialization
  - Create alignment-safe accessor macros
  - Add endianness detection
  - _Requirements: 12.1, 12.2, 12.5, 51.2, 51.3, 52.2_

- [ ]* 5.1 Write property test for serialization
  - **Property 3: Serialization Round-Trip**
  - **Validates: Requirements 12.1, 12.2, 51.2, 51.3**

- [x] 6. Implement list data structure
  - Create src/core/xgl_list.c and xgl_list.h
  - Implement intrusive doubly-linked list
  - Add list operations (insert, remove, iterate)
  - Add thread-safe variants (if XGL_THREAD_SAFE enabled)
  - _Requirements: 9.1_

- [x] 7. Implement hash table for routing
  - Create src/core/xgl_hashtable.c and xgl_hashtable.h
  - Implement hash table with O(1) average lookup
  - Add collision handling (chaining)
  - _Requirements: 4.1, 4.2_

- [x] 8. Implement memory pool
  - Create src/core/xgl_mempool.c and xgl_mempool.h
  - Implement fixed-size block memory pool
  - Add allocation and deallocation functions
  - Track pool statistics (used, free, peak)
  - Add thread-safe variants (if enabled)
  - _Requirements: 2.1, 15.2, 17.1, 17.2, 17.3_

- [x] 9. Implement tiered memory pool
  - Create src/core/xgl_tiered_pool.c and xgl_tiered_pool.h
  - Create small pool (≤64 bytes)
  - Create medium pool (≤256 bytes)
  - Create large pool (≤1024 bytes)
  - Implement smart allocation (selects appropriate pool)
  - _Requirements: 15.2, 17.1_

- [x] 10. Implement packet object pool
  - Create src/core/xgl_packet_pool.c and xgl_packet_pool.h
  - Implement pre-allocated packet objects
  - Add free list management
  - Add reference counting support
  - Track object pool statistics
  - _Requirements: 17.1, 17.2, 17.3, 17.4, 17.5_


- [x] 11. Implement custom allocator support
  - Create src/core/xgl_allocator.c and xgl_allocator.h
  - Define allocator interface abstraction
  - Implement default allocator (malloc/free wrapper)
  - Add allocator wrapper for tracking
  - _Requirements: 2.1, 10.4_

- [ ]* 11.1 Write property test for custom allocator
  - **Property 7: Custom Allocator Usage**
  - **Validates: Requirements 2.1**

- [ ]* 11.2 Write property test for memory management
  - **Property 1: Memory Leak Prevention**
  - **Property 8: Allocation Failure Handling**
  - **Property 9: Memory Pool Exhaustion**
  - **Validates: Requirements 1.3, 2.2, 2.3, 2.5, 8.3**

- [x] 12. Checkpoint - Ensure core utilities work correctly
  - Ensure all tests pass, ask the user if questions arise.

- [x] 13. Implement frame structure and encapsulation
  - Create src/datalink/xgl_frame.h with frame format definitions
  - Create src/datalink/xgl_frame.c
  - Implement xgl_frame_build() to construct frame from packet
  - Add SOF, header, payload, CRC8, CRC16
  - Support zero-copy mode
  - Handle attribute encoding
  - _Requirements: 3.1, 16.1, 16.2_

- [x] 14. Implement frame parser state machine
  - Create src/datalink/xgl_parser.c and xgl_parser.h
  - Implement state machine (SOF, HEADER, PAYLOAD, CRC)
  - Add byte-by-byte parsing
  - Add timeout handling for incomplete frames
  - Add parser reset on errors
  - _Requirements: 3.2_

- [x] 15. Implement data link layer interface
  - Create src/datalink/xgl_datalink.c and xgl_datalink.h
  - Implement xgl_datalink_send() to send frame via PHY
  - Implement xgl_datalink_receive() to receive and parse frames
  - Add frame validation (CRC8, CRC16)
  - Track error statistics
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [ ]* 15.1 Write property test for frame handling
  - **Property 2: CRC Error Detection**
  - **Property 5: Frame Encapsulation Round-Trip**
  - **Property 27: Field Validation**
  - **Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 12.3, 13.4**


- [x] 16. Checkpoint - Ensure data link layer works correctly
  - Ensure all tests pass, ask the user if questions arise.

- [x] 17. Implement route table
  - Create src/network/xgl_route.c and xgl_route.h
  - Implement route table initialization
  - Implement route lookup using hash table
  - Add route add/remove for dynamic routing
  - Add route metrics support
  - _Requirements: 4.1, 4.2, 26.1, 26.2, 26.3_

- [x] 18. Implement network layer packet handling
  - Create src/network/xgl_network.c and xgl_network.h
  - Implement xgl_network_send() to route and send packet
  - Implement xgl_network_receive() to receive and forward packet
  - Add address validation
  - Add packet forwarding logic
  - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_

- [x] 19. Implement sequence number management
  - Create src/network/xgl_sequence.c and xgl_sequence.h
  - Implement per-route sequence number tracking
  - Add sequence number assignment
  - Handle wraparound (0-255)
  - _Requirements: 7.1, 7.2_

- [ ]* 19.1 Write property test for network layer
  - **Property 10: Route Lookup Correctness**
  - **Property 11: Route Not Found Handling**
  - **Property 12: Packet Forwarding to Self**
  - **Property 21: Sequence Number Monotonicity**
  - **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 7.1**

- [x] 20. Checkpoint - Ensure network layer works correctly
  - Ensure all tests pass, ask the user if questions arise.

- [x] 21. Implement RTT estimator
  - Create src/transport/xgl_rtt.c and xgl_rtt.h
  - Implement RTT estimation (RFC 6298 algorithm)
  - Calculate SRTT and RTTVAR
  - Calculate RTO (SRTT + 4 * RTTVAR)
  - Add RTO clamping (min/max bounds)
  - _Requirements: 6.1, 6.2, 6.3_

- [ ]* 21.1 Write property test for RTT estimation
  - **Property 18: RTT Estimation**
  - **Property 19: RTO Calculation**
  - **Validates: Requirements 6.1, 6.2**


- [x] 22. Implement sliding window
  - Create src/transport/xgl_window.c and xgl_window.h
  - Implement sliding window state management
  - Add window advancement on ACK
  - Add window full detection
  - Track ACK bitmap
  - _Requirements: 7.5, 24.1, 24.2, 24.3_

- [ ]* 22.1 Write property test for sliding window
  - **Property 23: Sliding Window Maintenance**
  - **Validates: Requirements 7.5**

- [x] 23. Implement reliable transmission queue
  - Create src/transport/xgl_reliable.c and xgl_reliable.h
  - Implement wait-ACK queue management
  - Add packet timeout tracking
  - Implement retransmission logic
  - Add exponential backoff
  - Track retry count
  - _Requirements: 5.1, 5.2, 5.3, 6.4_

- [ ]* 23.1 Write property test for reliable transmission
  - **Property 13: Reliable Transmission Queuing**
  - **Property 14: Retransmission on Timeout**
  - **Property 15: Retry Exhaustion Handling**
  - **Property 20: Exponential Backoff**
  - **Validates: Requirements 5.1, 5.2, 5.3, 6.4**

- [x] 24. Implement ACK/NACK handling
  - Create src/transport/xgl_ack.c and xgl_ack.h
  - Implement ACK packet generation
  - Implement ACK packet processing
  - Add duplicate detection
  - Handle out-of-order packets
  - _Requirements: 5.4, 5.5, 7.4_

- [ ]* 24.1 Write property test for ACK handling
  - **Property 16: ACK Processing**
  - **Property 17: ACK Generation**
  - **Property 22: Duplicate Packet Handling**
  - **Validates: Requirements 5.4, 5.5, 7.4**

- [x] 25. Implement fragmentation support
  - Create src/transport/xgl_fragment.c and xgl_fragment.h
  - Implement data fragmentation into multiple packets
  - Assign fragment IDs and offsets
  - Implement reassembly buffer management
  - Add reassembly timeout handling
  - _Requirements: 14.1, 14.2, 14.3, 14.4, 14.5_

- [ ]* 25.1 Write property test for fragmentation
  - **Property 29: Fragmentation Correctness**
  - **Property 30: Fragment Timeout Handling**
  - **Validates: Requirements 14.1, 14.2, 14.3, 14.4, 14.5**


- [x] 26. Implement transport layer main interface
  - Create src/transport/xgl_transport.c and xgl_transport.h
  - Implement xgl_transport_send() for sending with reliability
  - Implement xgl_transport_receive() to receive and process ACKs
  - Implement xgl_transport_run() for periodic processing (timeouts)
  - Integrate all transport components
  - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 6.1, 6.2, 6.4, 7.1, 7.4, 7.5_

- [x] 27. Checkpoint - Ensure transport layer works correctly
  - Ensure all tests pass, ask the user if questions arise.

- [x] 28. Implement platform abstraction - thread safety
  - Create src/platform/xgl_mutex.c and xgl_mutex.h
  - Define mutex interface
  - Implement POSIX implementation (pthread)
  - Implement FreeRTOS implementation
  - Implement Windows implementation
  - Implement no-op implementation (bare-metal)
  - _Requirements: 9.1, 9.2, 9.3, 9.5, 46.1_

- [x] 29. Implement platform abstraction - time
  - Create src/platform/xgl_time.c and xgl_time.h
  - Implement xgl_time_ms() to get current time
  - Implement xgl_delay_ms() for delays
  - Add platform-specific implementations
  - Add hardware timer support
  - _Requirements: 47.1, 47.2, 47.3_

- [x] 30. Implement platform abstraction - atomic operations
  - Create src/platform/xgl_atomic.h
  - Implement atomic increment/decrement
  - Implement atomic load/store
  - Use C11 stdatomic.h wrapper
  - Add fallback for non-atomic platforms
  - _Requirements: 2.4, 9.2_

- [x] 31. Implement platform detection
  - Create src/platform/xgl_platform.h
  - Add compiler detection (GCC, Clang, MSVC)
  - Add OS detection (Linux, Windows, FreeRTOS, bare-metal)
  - Add architecture detection (ARM, x86, x64)
  - Add endianness detection
  - Add alignment requirements detection
  - _Requirements: 28.1, 28.4, 28.5, 51.1, 52.4_

- [x] 32. Checkpoint - Ensure platform abstraction works correctly
  - Ensure all tests pass, ask the user if questions arise.


- [x] 33. Implement instance management API
  - Create src/core/xgl_instance.c
  - Update include/xgl/xgl.h
  - Implement xgl_create() to create protocol instance
  - Implement xgl_init() to initialize instance
  - Implement xgl_destroy() to destroy instance
  - Add instance structure allocation
  - Add configuration validation
  - _Requirements: 1.1, 1.2, 1.3, 10.2_

- [ ]* 33.1 Write property test for instance management
  - **Property 6: Instance Isolation**
  - **Validates: Requirements 1.4**

- [x] 34. Implement configuration API
  - Create src/core/xgl_config.c
  - Implement xgl_config_get_default() for default config
  - Add configuration presets (tiny, small, medium, large)
  - Add configuration validation
  - Add configuration documentation
  - _Requirements: 10.1, 10.2, 10.3, 42.3_

- [x] 35. Implement send API
  - Create src/core/xgl_send.c
  - Implement xgl_send() for standard send with copy
  - Implement xgl_send_zerocopy() for zero-copy send
  - Add parameter validation
  - Integrate with transport layer
  - _Requirements: 16.1, 16.2, 16.3, 16.5_

- [x] 36. Implement statistics API
  - Create src/core/xgl_stats.c
  - Implement xgl_stats_get() to get current statistics
  - Implement xgl_stats_reset() to reset statistics
  - Add statistics structure population
  - Use atomic operations for statistics updates
  - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 27.1, 27.2, 27.3_

- [ ]* 36.1 Write property test for error handling
  - **Property 24: Error Code Specificity**
  - **Property 25: Error Callback Invocation**
  - **Property 26: Error Statistics**
  - **Validates: Requirements 8.1, 8.2, 8.4, 8.5**

- [x] 37. Update main public header
  - Update include/xgl/xgl.h
  - Include all public headers
  - Add comprehensive API documentation
  - Add usage examples in comments
  - Add version macros
  - _Requirements: 29.1, 30.1, 32.1, 32.2, 32.3, 32.4_


- [x] 38. Checkpoint - Ensure public API works correctly
  - Ensure all tests pass, ask the user if questions arise.

- [ ]* 39. Setup test infrastructure with Google Test
  - Create test/CMakeLists.txt with Google Test integration
  - Setup FetchContent for Google Test v1.14.0
  - Create test/mocks/ directory with mock objects
  - Create mock_phy.h for physical layer mocking
  - Create mock_allocator.h for allocator mocking
  - Create mock_callbacks.h for callback mocking
  - Create test/property/property_framework.h for property test utilities
  - Add random input generators for protocol structures
  - _Requirements: 19.1, 19.2, 19.3_

- [ ]* 40. Create integration test suite
  - Create test/integration/test_integration.cpp
  - Add end-to-end send/receive test using Google Test
  - Add multi-instance test
  - Add stress test (high load)
  - Add error recovery test
  - Use Google Mock for PHY layer simulation
  - _Requirements: 19.2_

- [ ]* 41. Write property test for alignment safety
  - **Property 28: Alignment Safety**
  - **Validates: Requirements 12.5, 52.2**

- [ ]* 41. Create echo server example
  - Create examples/echo_server/
  - Implement simple echo server
  - Demonstrate basic send/receive
  - Add README with explanation
  - _Requirements: 40.1, 40.2_

- [ ]* 42. Create file transfer example
  - Create examples/file_transfer/
  - Implement reliable file transfer
  - Demonstrate fragmentation
  - Add progress reporting
  - Add error handling
  - Add README with explanation
  - _Requirements: 40.1, 40.3_

- [ ]* 43. Create multi-node network example
  - Create examples/multi_node/
  - Implement three-node network simulation
  - Demonstrate routing
  - Show multiple instances
  - Show packet forwarding
  - Add README with explanation
  - _Requirements: 40.1, 40.4_

- [ ] 44. Final checkpoint - Ensure all core functionality works
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP delivery
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties (minimum 100 iterations)
- Unit tests validate specific examples and edge cases
- All tests use **Google Test (gtest)** and **Google Mock (gmock)**
- Test files are written in C++ (.cpp) but test C code
- All code must follow Nexus comment standards (backslash-style Doxygen)
- Code must compile without warnings on GCC, Clang, and MSVC
- Memory leaks must be detected and fixed using Valgrind or AddressSanitizer

## Test Framework Details

### Google Test Integration

```cmake
# In CMakeLists.txt
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)
```

### Running Tests

```bash
# Build and run all tests
cmake --build build --target xgl_tests
./build/test/xgl_tests

# Run specific test
./build/test/xgl_tests --gtest_filter=XglMemoryTest.*

# Run with verbose output
./build/test/xgl_tests --gtest_verbose

# Generate XML report
./build/test/xgl_tests --gtest_output=xml:test_results.xml
```

