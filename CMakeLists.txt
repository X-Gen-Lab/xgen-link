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
# Source Files
#-----------------------------------------------------------------------------

include(cmake/XglSources.cmake)

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

#-----------------------------------------------------------------------------
# Tests
#-----------------------------------------------------------------------------

if(XGL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(cmake/XglDevelopment.cmake)

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

include(cmake/XglInstall.cmake)

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
