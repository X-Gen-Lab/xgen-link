cmake_minimum_required(VERSION 3.24)
include(cmake/XglVersion.cmake)
project(
    xgl
    VERSION "${XGL_VERSION}"
    DESCRIPTION "xgen-link Protocol Stack"
    LANGUAGES C
)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(
    XGL_BUILD_TESTS
    "Build regression tests matching the selected profile"
    OFF
)
option(XGL_BUILD_EXAMPLES "Build examples" OFF)
option(XGL_BUILD_DOCS "Build documentation" OFF)
option(XGL_ENABLE_COVERAGE "Enable code coverage" OFF)
option(XGL_BUILD_FOOTPRINT_REPORT "Build footprint report target" OFF)
option(
    XGL_BUILD_SDK_CONSUMER_SMOKE
    "Build install-package consumer smoke test target"
    OFF
)
option(
    XGL_BUILD_RELEASE_VALIDATION_TARGET
    "Build release validation helper target"
    OFF
)
option(XGL_BUILD_NOHEAP_SMOKE "Build strict no-heap profile smoke target" OFF)

set(xgl_host_checks_enabled FALSE)
if(XGL_BUILD_TESTS OR XGL_BUILD_NOHEAP_SMOKE OR XGL_BUILD_SDK_CONSUMER_SMOKE)
    if(CMAKE_CROSSCOMPILING)
        message(
            FATAL_ERROR
            "XGL_BUILD_TESTS, XGL_BUILD_NOHEAP_SMOKE and XGL_BUILD_SDK_CONSUMER_SMOKE require a separate native host build"
        )
    endif()
    set(xgl_host_checks_enabled TRUE)
    enable_testing()
endif()

include(cmake/XglProfiles.cmake)
include(cmake/XglLibrary.cmake)
xgl_add_library(xgl "${XGL_ALLOW_FALLBACK_MALLOC}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_library(xgl::xgl ALIAS xgl)
target_include_directories(
    xgl
    PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

if(XGL_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CROSSCOMPILING)
        target_compile_options(xgl PRIVATE --coverage -O0 -g)
        target_link_options(xgl PUBLIC --coverage)
    else()
        message(
            FATAL_ERROR
            "XGL_ENABLE_COVERAGE requires a native GNU compiler and gcov"
        )
    endif()
endif()

include(cmake/XglInstall.cmake)

if(XGL_BUILD_TESTS)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_language(CXX)
    add_subdirectory(test)
endif()

include(cmake/XglDevelopment.cmake)

if(XGL_BUILD_EXAMPLES)
    add_subdirectory(examples)
    if(TARGET xgl_release_validation)
        foreach(example echo_server file_transfer multi_node boot_update)
            if(TARGET ${example})
                add_dependencies(xgl_release_validation ${example})
            endif()
        endforeach()
    endif()
endif()

if(XGL_BUILD_DOCS)
    add_subdirectory(docs)
    if(TARGET xgl_release_validation)
        add_dependencies(xgl_release_validation xgl_docs)
    endif()
endif()
