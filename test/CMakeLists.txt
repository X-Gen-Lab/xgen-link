# Test Configuration
# Google Test and Google Mock integration
# Author: X-Gen Lab

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "GoogleTest requires a separate native host build")
endif()

find_package(GTest 1.16.0 EXACT CONFIG REQUIRED)

include(TestSources.cmake)
add_executable(xgl_tests ${XGL_TEST_SOURCES})
xgl_target_owned_warnings(xgl_tests)

target_link_libraries(xgl_tests PRIVATE xgl GTest::gtest GTest::gmock)

target_include_directories(
    xgl_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/support/mocks
        ${CMAKE_CURRENT_SOURCE_DIR}/support
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/property
)

# Fix PDB locking issues on Windows with parallel compilation
if(MSVC)
    target_compile_options(xgl_tests PRIVATE /FS /permissive-)
else()
    target_compile_options(xgl_tests PRIVATE -pedantic-errors)
endif()

add_executable(xgl_interop_peer interop/interop_peer.c)
target_link_libraries(xgl_interop_peer PRIVATE xgl::xgl)

include(cmake/HeaderContracts.cmake)
include(GoogleTest)
gtest_discover_tests(
    xgl_tests
    DISCOVERY_MODE POST_BUILD
    TEST_LIST xgl_discovered_tests
    PROPERTIES TIMEOUT 30
)
set_property(
    DIRECTORY
    APPEND
    PROPERTY
        TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/profile-tests.cmake"
)
file(
    GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/profile-tests.cmake"
    CONTENT
        "set(xgl_complete_regression_suite ${XGL_FEATURE_AUTH})\ninclude(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/classify_tests.cmake\")\n"
)

# Run individually discovered protocol cases with the xgl label.
add_custom_target(
    run_tests
    COMMAND
        ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -L xgl --no-tests=error
        --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running protocol tests..."
)

# Run property tests only
add_custom_target(
    run_property_tests
    COMMAND
        ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -L property --no-tests=error
        --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running property tests..."
)

# Run unit tests only
add_custom_target(
    run_unit_tests
    COMMAND
        ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -L unit --no-tests=error
        --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running unit tests..."
)

# Keep the existing shortcut for the layered statistics suite.
add_custom_target(
    run_architecture_tests
    COMMAND
        ${CMAKE_CTEST_COMMAND} -C $<CONFIG> -R XglLayeredStatsTest
        --no-tests=error --output-on-failure
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running layered statistics tests..."
)

# Generate GoogleTest XML for the host test executable.
add_custom_target(
    test_report
    COMMAND xgl_tests --gtest_output=xml:test_results.xml
    DEPENDS xgl_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating test report..."
)

option(XGL_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(XGL_BUILD_BENCHMARKS)
    include(benchmark/Targets.cmake)
endif()
