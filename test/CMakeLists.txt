# Test Configuration
# Google Test and Google Mock integration
# Author: Nexus Team

#-----------------------------------------------------------------------------
# Google Test Setup
#-----------------------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

#-----------------------------------------------------------------------------
# Test Sources
#-----------------------------------------------------------------------------

set(TEST_SOURCES
    # Core tests
    test_types.cpp
    test_crc.cpp
    test_serialize.cpp
    test_list.cpp
    test_hashtable.cpp
    test_mempool.cpp
    test_tiered_pool.cpp
    test_packet_pool.cpp
    test_allocator.cpp
    test_instance.cpp
    test_config.cpp
    test_send.cpp
    test_stats.cpp
    
    # Architecture improvement tests
    test_layered_stats.cpp
    test_time_provider.cpp
    
    # Data link tests
    test_frame.cpp
    test_parser.cpp
    test_datalink.cpp
    
    # Network tests
    test_route.cpp
    test_network.cpp
    test_sequence.cpp
    
    # Transport tests
    test_rtt.cpp
    test_window.cpp
    test_reliable.cpp
    test_ack.cpp
    test_fragment.cpp
    test_transport.cpp
    test_timeout_control.cpp
    
    # Platform tests
    test_mutex.cpp
    test_time.cpp
    test_atomic.cpp
    test_platform.cpp
    
    # Integration tests
    integration/test_integration.cpp
    
    # Property tests
    property/test_memory_properties.cpp
    property/test_crc_properties.cpp
    property/test_serialization_properties.cpp
    property/test_frame_properties.cpp
    property/test_network_properties.cpp
    property/test_transport_properties.cpp
    property/test_fragment_properties.cpp
    property/test_instance_properties.cpp
    property/test_error_properties.cpp
    property/test_alignment_properties.cpp
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
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
)

target_include_directories(xgl_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/property
)

# Fix PDB locking issues on Windows with parallel compilation
if(MSVC)
    target_compile_options(xgl_tests PRIVATE /FS)
endif()

#-----------------------------------------------------------------------------
# CTest Entry
#-----------------------------------------------------------------------------

add_test(NAME xgl_tests COMMAND xgl_tests)
set_tests_properties(xgl_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 30
)

#-----------------------------------------------------------------------------
# Custom Test Targets
#-----------------------------------------------------------------------------

# Run all tests
add_custom_target(run_tests
    COMMAND xgl_tests
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all tests..."
)

# Run property tests only
add_custom_target(run_property_tests
    COMMAND xgl_tests --gtest_filter=*Property*
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running property tests..."
)

# Run unit tests only
add_custom_target(run_unit_tests
    COMMAND xgl_tests --gtest_filter=-*Property*
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests..."
)

# Run architecture improvement tests only
add_custom_target(run_architecture_tests
    COMMAND xgl_tests --gtest_filter=*LayeredStats*:*TimeProvider*
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
