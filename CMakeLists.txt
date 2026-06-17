# xgen-link Protocol Stack
# Modern embedded communication protocol implementation
# Author: X-Gen Lab

cmake_minimum_required(VERSION 3.21)

#-----------------------------------------------------------------------------
# Project Configuration
#-----------------------------------------------------------------------------

project(xgl
    VERSION 2.0.0
    DESCRIPTION "xgen-link Protocol Stack"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# C11 standard required
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# C++20 for tests (required for designated initializers)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#-----------------------------------------------------------------------------
# Build Options
#-----------------------------------------------------------------------------

option(XGL_BUILD_TESTS "Build tests" OFF)
option(XGL_BUILD_EXAMPLES "Build examples" ON)
option(XGL_BUILD_DOCS "Build documentation" OFF)
option(XGL_ENABLE_COVERAGE "Enable code coverage" OFF)
option(XGL_BUILD_FOOTPRINT_REPORT "Build footprint report target" ON)
option(XGL_BUILD_STATIC_ANALYSIS_TARGET "Build static analysis helper target" ON)
option(XGL_BUILD_SDK_CONSUMER_SMOKE "Build install-package consumer smoke test target" ON)
option(XGL_BUILD_RELEASE_VALIDATION_TARGET "Build release validation helper target" ON)
option(XGL_ALLOW_FALLBACK_MALLOC "Allow NULL allocator to fall back to malloc/free" ON)
option(XGL_BUILD_NOHEAP_SMOKE "Build strict no-heap profile smoke target" ON)

#-----------------------------------------------------------------------------
# Compiler Warnings
#-----------------------------------------------------------------------------

set(XGL_C_WARNINGS)
set(XGL_CXX_WARNINGS)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND XGL_C_WARNINGS
        -Wall
        -Wextra
        -Werror
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wcast-align
        -Wcast-qual
        -Wunused
        -Wno-unused-parameter
    )
elseif(MSVC)
    list(APPEND XGL_C_WARNINGS
        /W4
        /WX
        /permissive-
    )
endif()

#-----------------------------------------------------------------------------
# Include Directories
#-----------------------------------------------------------------------------

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

#-----------------------------------------------------------------------------
# Source Files
#-----------------------------------------------------------------------------

set(XGL_SOURCES
    # Public API orchestration
    src/api/xgl_instance.c
    src/api/xgl_instance_lifecycle.c
    src/api/xgl_runtime.c
    src/api/xgl_config.c
    src/api/xgl_send.c
    src/api/xgl_stats.c
    src/api/xgl_version.c

    # Wire format and serialization
    src/wire/xgl_crc.c
    src/wire/xgl_serialize.c
    src/wire/xgl_wire_ack_ext.c
    src/wire/xgl_wire_ext.c
    src/wire/xgl_wire.c
    src/wire/xgl_frame.c
    src/wire/xgl_frame_zerocopy.c
    src/wire/xgl_parser.c

    # Security
    src/security/xgl_security.c

    # Core utilities
    src/core/xgl_error.c
    src/core/xgl_list.c
    src/core/xgl_list_ts.c
    src/core/xgl_hashtable.c

    # Memory management
    src/memory/xgl_mempool.c
    src/memory/xgl_mempool_ts.c
    src/memory/xgl_tiered_pool.c
    src/memory/xgl_tiered_pool_stats.c
    src/memory/xgl_packet_pool.c
    src/memory/xgl_allocator.c
    src/memory/xgl_tracking_allocator.c

    # Optional Modules
    src/codec/xgl_codec.c

    # Data Link Layer
    src/datalink/xgl_datalink_metadata.c
    src/datalink/xgl_datalink_send.c
    src/datalink/xgl_datalink_receive.c
    src/datalink/xgl_datalink.c

    # Network Layer
    src/network/xgl_route.c
    src/network/xgl_route_load.c
    src/network/xgl_route_lookup.c
    src/network/xgl_route_mutation.c
    src/network/xgl_network_metadata.c
    src/network/xgl_network_send.c
    src/network/xgl_network_receive.c
    src/network/xgl_network.c

    # Transport Layer
    src/transport/xgl_rtt.c
    src/transport/xgl_window.c
    src/transport/xgl_reliable.c
    src/transport/xgl_reliable_ack.c
    src/transport/xgl_fragment_range.c
    src/transport/xgl_fragment.c
    src/transport/xgl_transport_peer.c
    src/transport/xgl_transport_send_plan.c
    src/transport/xgl_transport_send_fragment.c
    src/transport/xgl_transport_send.c
    src/transport/xgl_transport_receive.c
    src/transport/xgl_transport_control.c
    src/transport/xgl_transport_retransmit.c
    src/transport/xgl_transport_rx_order.c
    src/transport/xgl_transport_ack.c
    src/transport/xgl_transport.c

    # Platform Abstraction
    src/platform/xgl_mutex.c
    src/platform/xgl_mutex_guard.c
    src/platform/xgl_mutex_noop.c
    src/platform/xgl_mutex_windows.c
    src/platform/xgl_time.c
    src/platform/xgl_timer.c
    src/platform/xgl_time_provider.c
    src/platform/xgl_platform.c
)

#-----------------------------------------------------------------------------
# Library Target
#-----------------------------------------------------------------------------

add_library(xgl STATIC ${XGL_SOURCES})

target_compile_options(xgl PRIVATE ${XGL_C_WARNINGS})
if(XGL_ALLOW_FALLBACK_MALLOC)
    target_compile_definitions(xgl PUBLIC XGL_ALLOW_FALLBACK_MALLOC=1)
else()
    target_compile_definitions(xgl PUBLIC XGL_ALLOW_FALLBACK_MALLOC=0)
endif()

target_include_directories(xgl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(XGL_BUILD_NOHEAP_SMOKE)
    add_library(xgl_noheap STATIC ${XGL_SOURCES})
    target_compile_options(xgl_noheap PRIVATE ${XGL_C_WARNINGS})
    target_compile_definitions(xgl_noheap PUBLIC XGL_ALLOW_FALLBACK_MALLOC=0)
    target_include_directories(xgl_noheap
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_executable(xgl_noheap_smoke tools/noheap_smoke.c)
    target_link_libraries(xgl_noheap_smoke PRIVATE xgl_noheap)
endif()

if(XGL_BUILD_FOOTPRINT_REPORT)
    set(XGL_FOOTPRINT_REPORT ${CMAKE_CURRENT_BINARY_DIR}/footprint/xgl-footprint.txt)
    add_custom_command(
        OUTPUT ${XGL_FOOTPRINT_REPORT}
        COMMAND ${CMAKE_COMMAND}
            -D XGL_TARGET_FILE=$<TARGET_FILE:xgl>
            -D XGL_REPORT_FILE=${XGL_FOOTPRINT_REPORT}
            -D XGL_PROJECT_VERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/footprint_report.cmake
        DEPENDS xgl ${CMAKE_CURRENT_SOURCE_DIR}/tools/footprint_report.cmake
        VERBATIM
    )
    add_custom_target(xgl_footprint DEPENDS ${XGL_FOOTPRINT_REPORT})
endif()

if(XGL_BUILD_STATIC_ANALYSIS_TARGET)
    find_program(XGL_CPPCHECK_EXE NAMES cppcheck)
    if(XGL_CPPCHECK_EXE)
        add_custom_target(xgl_static_analysis
            COMMAND ${XGL_CPPCHECK_EXE}
                --enable=warning,style,performance,portability
                --std=c11
                --inline-suppr
                --error-exitcode=1
                --suppress=toomanyconfigs
                -I ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${XGL_SOURCES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Running embedded C static analysis baseline"
            VERBATIM
        )
    else()
        add_custom_target(xgl_static_analysis
            COMMAND ${CMAKE_COMMAND} -E echo "cppcheck not found; install cppcheck to run the embedded C static analysis baseline"
            COMMENT "Static analysis baseline is unavailable"
            VERBATIM
        )
    endif()
endif()

if(XGL_BUILD_SDK_CONSUMER_SMOKE)
    add_custom_target(xgl_sdk_consumer_smoke
        COMMAND ${CMAKE_COMMAND}
            -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke
            -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk_consumer_smoke.cmake
        DEPENDS xgl
        COMMENT "Validating installed xgl CMake package with a consumer project"
        VERBATIM
    )
endif()

#-----------------------------------------------------------------------------
# Tests
#-----------------------------------------------------------------------------

if(XGL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
    if(XGL_BUILD_SDK_CONSUMER_SMOKE)
        add_test(NAME xgl_sdk_consumer_smoke
            COMMAND ${CMAKE_COMMAND}
                -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke-ctest
                -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk_consumer_smoke.cmake
        )
        set_tests_properties(xgl_sdk_consumer_smoke PROPERTIES TIMEOUT 60)
    endif()
    if(XGL_BUILD_NOHEAP_SMOKE)
        add_test(NAME xgl_noheap_smoke COMMAND xgl_noheap_smoke)
        set_tests_properties(xgl_noheap_smoke PROPERTIES TIMEOUT 30)
    endif()
endif()

if(XGL_BUILD_RELEASE_VALIDATION_TARGET)
    set(XGL_RELEASE_VALIDATION_DEPS xgl)
    if(XGL_BUILD_TESTS)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_tests)
    endif()
    if(XGL_BUILD_FOOTPRINT_REPORT)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_footprint)
    endif()
    if(XGL_BUILD_STATIC_ANALYSIS_TARGET)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_static_analysis)
    endif()
    if(XGL_BUILD_SDK_CONSUMER_SMOKE)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_sdk_consumer_smoke)
    endif()
    if(XGL_BUILD_NOHEAP_SMOKE)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_noheap_smoke)
    endif()

    add_custom_target(xgl_release_validation
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR} --output-on-failure
        DEPENDS ${XGL_RELEASE_VALIDATION_DEPS}
        COMMENT "Running xgl release validation checks"
        VERBATIM
    )
endif()

#-----------------------------------------------------------------------------
# Examples
#-----------------------------------------------------------------------------

if(XGL_BUILD_EXAMPLES)
    add_subdirectory(examples)
    if(TARGET xgl_release_validation)
        foreach(XGL_EXAMPLE_TARGET echo_server file_transfer multi_node)
            if(TARGET ${XGL_EXAMPLE_TARGET})
                add_dependencies(xgl_release_validation ${XGL_EXAMPLE_TARGET})
            endif()
        endforeach()
    endif()
endif()

#-----------------------------------------------------------------------------
# Documentation
#-----------------------------------------------------------------------------

if(XGL_BUILD_DOCS)
    add_subdirectory(docs)
    if(TARGET xgl_release_validation AND TARGET xgl_docs)
        add_dependencies(xgl_release_validation xgl_docs)
    endif()
endif()

#-----------------------------------------------------------------------------
# Installation
#-----------------------------------------------------------------------------

install(TARGETS xgl
    EXPORT xglTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

set(XGL_PUBLIC_HEADERS
    include/xgl/xgl.h
    include/xgl/xgl_config.h
    include/xgl/xgl_error.h
    include/xgl/xgl_types.h
)

install(FILES ${XGL_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xgl
)

install(EXPORT xglTargets
    FILE xglTargets.cmake
    NAMESPACE xgl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/xglConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/xglConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

#-----------------------------------------------------------------------------
# Code Coverage
#-----------------------------------------------------------------------------

if(XGL_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_compile_options(xgl PRIVATE --coverage)
        target_link_options(xgl PUBLIC --coverage)
    elseif(WIN32)
        message(STATUS "Code coverage is not enabled for Windows builds")
    endif()
endif()
