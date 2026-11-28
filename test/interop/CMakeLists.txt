cmake_minimum_required(VERSION 3.24)
project(xgl_wire_interop LANGUAGES C)

set(XGL_PROFILE "boot" CACHE STRING "Wire interop profile (boot or full)")
set_property(CACHE XGL_PROFILE PROPERTY STRINGS boot full)
if(NOT XGL_PROFILE MATCHES "^(boot|full)$")
    message(FATAL_ERROR "Wire interop requires XGL_PROFILE=boot or full")
endif()

set(XGL_INTEROP_SDK_PREFIX
    ""
    CACHE PATH
    "Use an installed SDK from this prefix"
)
set(XGL_INTEROP_SDK_VERSION "" CACHE STRING "Exact installed SDK version")
if(XGL_INTEROP_SDK_PREFIX)
    if(NOT XGL_INTEROP_SDK_VERSION)
        message(FATAL_ERROR "Set XGL_INTEROP_SDK_VERSION for an installed SDK")
    endif()
    file(REAL_PATH "${XGL_INTEROP_SDK_PREFIX}" sdk_prefix)
    set(CMAKE_PREFIX_PATH "${sdk_prefix}")
    set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
    set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
    unset(xgl_DIR CACHE)
    find_package(
        xgl
        "${XGL_INTEROP_SDK_VERSION}"
        EXACT
        CONFIG
        REQUIRED
        PATHS "${sdk_prefix}"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
    )
    include(
        "${CMAKE_CURRENT_SOURCE_DIR}/../contracts/sdk/VerifySdkPrefix.cmake"
    )
    xgl_verify_sdk_prefix("${sdk_prefix}")
    if(NOT xgl_PROFILE STREQUAL XGL_PROFILE)
        message(
            FATAL_ERROR
            "Selected SDK profile ${xgl_PROFILE} does not match ${XGL_PROFILE}"
        )
    endif()
else()
    if(XGL_INTEROP_SDK_VERSION)
        message(FATAL_ERROR "Set XGL_INTEROP_SDK_PREFIX for an installed SDK")
    endif()
    set(XGL_BUILD_TESTS OFF)
    add_subdirectory("../.." xgen-link)
endif()

add_executable(xgl_interop_peer interop_peer.c)
set_target_properties(
    xgl_interop_peer
    PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
)
target_link_libraries(xgl_interop_peer PRIVATE xgl::xgl)
