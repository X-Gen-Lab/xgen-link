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
include(cmake/RegressionTests.cmake)

option(XGL_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(XGL_BUILD_BENCHMARKS)
    include(benchmark/Targets.cmake)
endif()
