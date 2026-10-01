# xgen-link Protocol Stack
# Modern embedded communication protocol implementation
# Author: X-Gen Lab

cmake_minimum_required(VERSION 3.24)

#-----------------------------------------------------------------------------
# Project Configuration
#-----------------------------------------------------------------------------

project(xgl
    VERSION 3.0.0
    DESCRIPTION "xgen-link Protocol Stack"
    LANGUAGES C
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# C11 standard required
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Standard C++17 for host tests
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#-----------------------------------------------------------------------------
# Build Options
#-----------------------------------------------------------------------------

include(cmake/XglProfiles.cmake)
if(XGL_PROFILE STREQUAL "boot" OR NOT PROJECT_IS_TOP_LEVEL)
    set(XGL_HOST_DEFAULT OFF)
else()
    set(XGL_HOST_DEFAULT ON)
endif()
option(XGL_BUILD_TESTS "Build Full/Embedded regression tests" OFF)
option(XGL_BUILD_EXAMPLES "Build examples" ${XGL_HOST_DEFAULT})
if(XGL_PROFILE STREQUAL "boot" AND XGL_BUILD_TESTS)
    message(FATAL_ERROR "Boot excludes Full test capabilities; use XGL_BUILD_NOHEAP_SMOKE for its static lifecycle tests")
endif()
option(XGL_BUILD_DOCS "Build documentation" OFF)
option(XGL_ENABLE_COVERAGE "Enable code coverage" OFF)
option(XGL_BUILD_FOOTPRINT_REPORT "Build footprint report target" ${PROJECT_IS_TOP_LEVEL})
option(XGL_BUILD_STATIC_ANALYSIS_TARGET "Build static analysis helper target" ${PROJECT_IS_TOP_LEVEL})
if(XGL_BUILD_STATIC_ANALYSIS_TARGET)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
option(XGL_BUILD_SDK_CONSUMER_SMOKE "Build install-package consumer smoke test target" ${PROJECT_IS_TOP_LEVEL})
option(XGL_BUILD_RELEASE_VALIDATION_TARGET "Build release validation helper target" ${PROJECT_IS_TOP_LEVEL})
if(NOT XGL_PROFILE STREQUAL "full")
    set(XGL_HEAP_DEFAULT OFF)
else()
    set(XGL_HEAP_DEFAULT ON)
endif()
option(XGL_ALLOW_FALLBACK_MALLOC "Allow instance creation to select the explicit libc allocator backend" ${XGL_HEAP_DEFAULT})
if(XGL_PROFILE STREQUAL "boot" AND XGL_ALLOW_FALLBACK_MALLOC)
    message(FATAL_ERROR "The boot profile requires XGL_ALLOW_FALLBACK_MALLOC=OFF")
endif()
option(XGL_BUILD_NOHEAP_SMOKE "Build strict no-heap profile smoke target" ${PROJECT_IS_TOP_LEVEL})

include(cmake/XglDependencies.cmake)
if(XGL_BUILD_TESTS)
    enable_language(CXX)
endif()

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


#-----------------------------------------------------------------------------
# Source Files
#-----------------------------------------------------------------------------

set(XGL_SOURCES
    # Public API orchestration
    src/api/xgl_instance.c
    src/api/xgl_instance_links.c
    src/api/xgl_workspace.c
    src/api/xgl_runtime.c
    src/api/xgl_config.c
    src/api/xgl_send.c
    src/api/xgl_send_zerocopy.c
    src/api/xgl_security_session.c
    src/api/xgl_stats.c
    src/api/xgl_version.c

    # Wire format and serialization
    src/wire/xgl_wire_ack_ext.c
    src/wire/xgl_wire_ext.c
    src/wire/xgl_wire.c
    src/wire/xgl_frame.c
    src/security/xgl_security_frame.c
    src/wire/xgl_frame_zerocopy.c
    src/wire/xgl_parser.c

    # Security
    src/security/xgl_security.c

    # Protocol error domain
    src/api/xgl_error.c

    # Data Link Layer
    src/datalink/xgl_datalink_metadata.c
    src/datalink/xgl_datalink_send.c
    src/datalink/xgl_datalink_receive.c
    src/datalink/xgl_datalink.c

    # Network Layer
    src/network/xgl_route.c
    src/network/xgl_network_send.c
    src/network/xgl_network_receive.c
    src/network/xgl_network_forward.c
    src/network/xgl_network.c

    # Transport Layer
    src/transport/xgl_rtt.c
    src/transport/xgl_window.c
    src/transport/xgl_reliable_packet.c
    src/transport/xgl_reliable.c
    src/transport/xgl_reliable_timeout.c
    src/transport/xgl_reliable_extensions.c
    src/transport/xgl_fragment_range.c
    src/transport/xgl_fragment_reassembly.c
    src/transport/xgl_fragment_maintenance.c
    src/transport/xgl_fragment_process.c
    src/transport/xgl_fragment.c
    src/transport/xgl_transport_memory.c
    src/transport/xgl_transport_packet_number.c
    src/transport/xgl_transport_peer.c
    src/transport/xgl_transport_send_plan.c
    src/transport/xgl_transport_send_packet.c
    src/transport/xgl_transport_send_fragment.c
    src/transport/xgl_transport_send.c
    src/transport/xgl_transport_receive.c
    src/transport/xgl_transport_receive_ack.c
    src/transport/xgl_transport_receive_peer.c
    src/transport/xgl_transport_receive_order.c
    src/transport/xgl_transport_rx_buffer.c
    src/transport/xgl_transport_delivery.c
    src/transport/xgl_transport_control.c
    src/transport/xgl_transport_retransmit.c
    src/transport/xgl_transport_rx_order.c
    src/transport/xgl_transport_ack.c
    src/transport/xgl_transport_ack_send.c
    src/transport/xgl_transport_sack.c
    src/transport/xgl_transport_sack_send.c
    src/transport/xgl_transport_interface.c
    src/transport/xgl_transport_runtime.c
    src/transport/xgl_transport.c

)

#-----------------------------------------------------------------------------
# Library Target
#-----------------------------------------------------------------------------

if(NOT XGL_FEATURE_AUTH)
    list(REMOVE_ITEM XGL_SOURCES src/security/xgl_security.c src/security/xgl_security_frame.c)
endif()
if(NOT XGL_FEATURE_FRAGMENTATION)
    list(FILTER XGL_SOURCES EXCLUDE REGEX "src/transport/xgl_fragment.*\\.c$")
    list(REMOVE_ITEM XGL_SOURCES src/transport/xgl_transport_send_fragment.c)
endif()
if(NOT XGL_FEATURE_OUT_OF_ORDER)
    list(REMOVE_ITEM XGL_SOURCES
        src/transport/xgl_transport_rx_buffer.c
        src/transport/xgl_transport_rx_order.c
        src/transport/xgl_transport_sack_send.c)
endif()


add_library(xgl STATIC ${XGL_SOURCES})
add_library(xgl::xgl ALIAS xgl)
target_compile_features(xgl PUBLIC c_std_11)
target_link_libraries(xgl PUBLIC xgm::allocator
    PRIVATE ${XGL_PRIVATE_DEPENDENCIES})

target_compile_options(xgl PRIVATE ${XGL_C_WARNINGS})
if(XGL_ALLOW_FALLBACK_MALLOC)
    if(NOT TARGET xgm::libc_allocator)
        message(FATAL_ERROR "Heap compatibility requires xgm::libc_allocator; otherwise disable XGL_ALLOW_FALLBACK_MALLOC")
    endif()
    target_link_libraries(xgl PRIVATE xgm::libc_allocator)
    target_compile_definitions(xgl PUBLIC XGL_ALLOW_FALLBACK_MALLOC=1)
else()
    target_compile_definitions(xgl PUBLIC XGL_ALLOW_FALLBACK_MALLOC=0)
endif()

target_include_directories(xgl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(XGL_BUILD_NOHEAP_SMOKE)
    add_library(xgl_noheap STATIC ${XGL_SOURCES})
    target_compile_features(xgl_noheap PUBLIC c_std_11)
    target_link_libraries(xgl_noheap PUBLIC xgm::allocator
        PRIVATE ${XGL_PRIVATE_DEPENDENCIES})
    target_compile_options(xgl_noheap PRIVATE ${XGL_C_WARNINGS})
    target_compile_definitions(xgl_noheap PUBLIC XGL_ALLOW_FALLBACK_MALLOC=0)
    target_include_directories(xgl_noheap
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(NOT XGL_PROFILE STREQUAL "boot")
        add_executable(xgl_noheap_smoke tools/noheap_smoke.c)
        target_link_libraries(xgl_noheap_smoke PRIVATE xgl_noheap)
    endif()
    add_executable(xgl_static_workspace_smoke tools/static_workspace_smoke.c)
    target_link_libraries(xgl_static_workspace_smoke PRIVATE xgl_noheap)
    enable_testing()
    add_test(NAME xgl_static_workspace_smoke COMMAND xgl_static_workspace_smoke)
    set_tests_properties(xgl_static_workspace_smoke PROPERTIES
        LABELS integration TIMEOUT 30)
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
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(xgl_static_analysis
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/quality.py
            cppcheck --build-dir ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running the shared static-analysis policy on the actual build"
        VERBATIM
    )
endif()

if(XGL_BUILD_SDK_CONSUMER_SMOKE)
    if(NOT DEFINED XGL_SDK_INSTALL_BINARY_DIR)
        set(XGL_SDK_INSTALL_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    # An SDK built against installed dependencies does not install them again.
    # Preserve their explicit discovery locations for the isolated consumer.
    set(XGL_SDK_DEPENDENCY_HINTS "${CMAKE_CURRENT_BINARY_DIR}/sdk-dependency-hints.cmake")
    file(WRITE "${XGL_SDK_DEPENDENCY_HINTS}"
        "set(XGL_UPSTREAM_PREFIX_PATH [==[${CMAKE_PREFIX_PATH}]==])\n")
    foreach(dependency STATUS BYTES CRC MEMORY CONTAINERS)
        string(TOLOWER "${dependency}" package_suffix)
        set(package_dir "xgen_${package_suffix}_DIR")
        if(DEFINED ${package_dir} AND IS_DIRECTORY "${${package_dir}}")
            file(APPEND "${XGL_SDK_DEPENDENCY_HINTS}"
                "set(${package_dir} [==[${${package_dir}}]==] CACHE PATH \"Explicit dependency package\")\n")
        endif()
    endforeach()
    add_custom_target(xgl_sdk_consumer_smoke
        COMMAND ${CMAKE_COMMAND}
            -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D XGL_INSTALL_BINARY_DIR=${XGL_SDK_INSTALL_BINARY_DIR}
            -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke
            -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
            -D XGL_DEPENDENCY_HINTS=${XGL_SDK_DEPENDENCY_HINTS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk_consumer_smoke.cmake
        DEPENDS xgl ${XGL_SDK_DEPENDENCY_TARGETS}
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
endif()

if(XGL_BUILD_SDK_CONSUMER_SMOKE)
    enable_testing()
    add_test(NAME xgl_sdk_consumer_smoke
        COMMAND ${CMAKE_COMMAND}
            -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D XGL_INSTALL_BINARY_DIR=${XGL_SDK_INSTALL_BINARY_DIR}
            -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke-ctest
            -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
            -D XGL_DEPENDENCY_HINTS=${XGL_SDK_DEPENDENCY_HINTS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk_consumer_smoke.cmake
    )
    set_tests_properties(xgl_sdk_consumer_smoke PROPERTIES
        LABELS integration TIMEOUT 60)
endif()

if(TARGET xgl_noheap_smoke)
    add_test(NAME xgl_noheap_smoke COMMAND xgl_noheap_smoke)
    set_tests_properties(xgl_noheap_smoke PROPERTIES
        LABELS integration TIMEOUT 30)
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
    if(TARGET xgl_noheap_smoke)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_noheap_smoke)
    endif()
    if(TARGET xgl_static_workspace_smoke)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_static_workspace_smoke)
    endif()

    add_custom_target(xgl_release_validation
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR} --output-on-failure --no-tests=error
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
        foreach(XGL_EXAMPLE_TARGET echo_server file_transfer multi_node boot_update)
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
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/xgl/xgl_build_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xgl)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/XglDependencyVersions.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xgl
)

#-----------------------------------------------------------------------------
# Code Coverage
#-----------------------------------------------------------------------------

if(XGL_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CROSSCOMPILING)
        target_compile_options(xgl PRIVATE --coverage -O0 -g)
        target_link_options(xgl PUBLIC --coverage)
    else()
        message(FATAL_ERROR "XGL_ENABLE_COVERAGE requires a native GNU compiler and gcov")
    endif()
endif()
