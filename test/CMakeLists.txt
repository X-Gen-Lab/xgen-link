# Test Configuration
# Google Test and Google Mock integration
# Author: X-Gen Lab

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "GoogleTest requires a separate native host build")
endif()

#-----------------------------------------------------------------------------
# Google Test Setup
#-----------------------------------------------------------------------------

find_package(GTest 1.16.0 EXACT CONFIG REQUIRED)

#-----------------------------------------------------------------------------
# Test Sources
#-----------------------------------------------------------------------------

set(TEST_SOURCES
    test_main.cpp
    test_cpp17_presets.cpp
    test_coverage_wire.cpp
    test_coverage_api.cpp
    test_coverage_delivery.cpp
    # Core tests
    test_types.cpp
    test_instance.cpp
    test_config.cpp
    test_creation_policy.cpp
    test_footprint.cpp
    test_send.cpp
    test_stats.cpp
    test_security.cpp

    # Architecture improvement tests
    test_layered_stats.cpp

    # Data link tests
    test_frame.cpp
    test_wire.cpp
    test_parser.cpp
    test_datalink.cpp

    # Network tests
    test_route.cpp
    test_network.cpp

    # Transport tests
    test_rtt.cpp
    test_window.cpp
    test_window_storage.cpp
    test_reliable.cpp
    test_fragment.cpp
    test_transport.cpp
    test_timeout_control.cpp

    # Platform tests

    # Integration tests
    integration/test_integration.cpp

    # Property tests
    property/test_memory_properties.cpp
    property/test_frame_properties.cpp
    property/test_network_properties.cpp
    property/test_transport_properties.cpp
    property/test_fragment_properties.cpp
    property/test_instance_properties.cpp
    property/test_error_properties.cpp
    property/test_alignment_properties.cpp
    property/test_generator.cpp
)

#-----------------------------------------------------------------------------
# Mock Objects
#-----------------------------------------------------------------------------

set(MOCK_SOURCES
    mocks/mock_phy.cpp
    mocks/mock_allocator.cpp
    mocks/mock_callbacks.cpp
)

#-----------------------------------------------------------------------------
# Test Executable
#-----------------------------------------------------------------------------

add_executable(xgl_tests
    ${TEST_SOURCES}
    ${MOCK_SOURCES}
)


target_link_libraries(xgl_tests
    PRIVATE
        xgl
        xgm::libc_allocator
        ${XGL_PRIVATE_DEPENDENCIES}
        GTest::gtest
        GTest::gmock
)

target_include_directories(xgl_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/property
)

# Fix PDB locking issues on Windows with parallel compilation
if(MSVC)
    target_compile_options(xgl_tests PRIVATE /FS /permissive-)
else()
    target_compile_options(xgl_tests PRIVATE -pedantic-errors)
endif()

#-----------------------------------------------------------------------------
# CTest Entry
#-----------------------------------------------------------------------------

include(cmake/HeaderContracts.cmake)
include(GoogleTest)
gtest_discover_tests(xgl_tests
    DISCOVERY_MODE POST_BUILD
    TEST_LIST xgl_discovered_tests
    PROPERTIES TIMEOUT 30
)
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/classify_tests.cmake")

#-----------------------------------------------------------------------------
# Custom Test Targets
#-----------------------------------------------------------------------------

# Run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -L xgl
        --no-tests=error --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all tests..."
)

# Run property tests only
add_custom_target(run_property_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -L property
        --no-tests=error --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running property tests..."
)

# Run unit tests only
add_custom_target(run_unit_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -L unit
        --no-tests=error --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests..."
)

# Run architecture improvement tests only
add_custom_target(run_architecture_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -R XglLayeredStatsTest
        --no-tests=error --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running architecture improvement tests..."
)

# Generate XML report
add_custom_target(test_report
    COMMAND xgl_tests --gtest_output=xml:test_results.xml
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating test report..."
)

#-----------------------------------------------------------------------------
# Benchmark Targets (optional, OFF by default)
#-----------------------------------------------------------------------------

option(XGL_BUILD_BENCHMARKS "Build performance benchmarks" OFF)

if(XGL_BUILD_BENCHMARKS)
    # Throughput benchmark
    add_executable(bench_throughput
        benchmark/bench_throughput.cpp
        mocks/mock_phy.cpp
    )
    target_link_libraries(bench_throughput PRIVATE xgl xgm::libc_allocator GTest::gmock)
    target_include_directories(bench_throughput
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )

    # Latency benchmark
    add_executable(bench_latency
        benchmark/bench_latency.cpp
        mocks/mock_phy.cpp
    )
    target_link_libraries(bench_latency PRIVATE xgl xgm::libc_allocator GTest::gmock)
    target_include_directories(bench_latency
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )

    # Memory usage benchmark
    add_executable(bench_memory
        benchmark/bench_memory.cpp
        mocks/mock_phy.cpp
    )
    target_link_libraries(bench_memory PRIVATE xgl xgm::libc_allocator GTest::gmock)
    target_include_directories(bench_memory
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )

    # Convenience target to build all benchmarks
    add_custom_target(xgl_benchmarks
        DEPENDS bench_throughput bench_latency bench_memory
        COMMENT "Building all benchmarks..."
    )
endif()
