# xgen-link Protocol Stack
# Modern embedded communication protocol implementation
# Author: Nexus Team

cmake_minimum_required(VERSION 3.21)

#-----------------------------------------------------------------------------
# Project Configuration
#-----------------------------------------------------------------------------

project(xgl
    VERSION 1.0.0
    DESCRIPTION "xgen-link Protocol Stack"
    LANGUAGES C CXX
)

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
    # Core
    src/core/xgl_instance.c
    src/core/xgl_config.c
    src/core/xgl_send.c
    src/core/xgl_stats.c
    src/core/xgl_error.c
    src/core/xgl_crc.c
    src/core/xgl_serialize.c
    src/core/xgl_list.c
    src/core/xgl_hashtable.c
    src/core/xgl_mempool.c
    src/core/xgl_tiered_pool.c
    src/core/xgl_packet_pool.c
    src/core/xgl_allocator.c

    # Data Link Layer
    src/datalink/xgl_frame.c
    src/datalink/xgl_parser.c
    src/datalink/xgl_datalink.c

    # Network Layer
    src/network/xgl_route.c
    src/network/xgl_network.c
    src/network/xgl_sequence.c

    # Transport Layer
    src/transport/xgl_rtt.c
    src/transport/xgl_window.c
    src/transport/xgl_reliable.c
    src/transport/xgl_ack.c
    src/transport/xgl_fragment.c
    src/transport/xgl_transport.c

    # Platform Abstraction
    src/platform/xgl_mutex.c
    src/platform/xgl_time.c
    src/platform/xgl_time_provider.c
    src/platform/xgl_platform.c
)

#-----------------------------------------------------------------------------
# Library Target
#-----------------------------------------------------------------------------

add_library(xgl STATIC ${XGL_SOURCES})

target_compile_options(xgl PRIVATE ${XGL_C_WARNINGS})

target_include_directories(xgl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

#-----------------------------------------------------------------------------
# Tests
#-----------------------------------------------------------------------------

if(XGL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

#-----------------------------------------------------------------------------
# Examples
#-----------------------------------------------------------------------------

if(XGL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

#-----------------------------------------------------------------------------
# Documentation
#-----------------------------------------------------------------------------

if(XGL_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    endif()
endif()

#-----------------------------------------------------------------------------
# Installation
#-----------------------------------------------------------------------------

install(TARGETS xgl
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

#-----------------------------------------------------------------------------
# Code Coverage
#-----------------------------------------------------------------------------

if(XGL_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_compile_options(xgl PRIVATE --coverage)
        target_link_options(xgl PRIVATE --coverage)
    elseif(WIN32)
        message(STATUS "Code coverage is not enabled for Windows builds")
    endif()
endif()
