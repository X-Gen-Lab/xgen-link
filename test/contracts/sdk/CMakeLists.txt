cmake_minimum_required(VERSION 3.21)
project(xgl_consumer_smoke C)

find_package(xgl CONFIG REQUIRED PATHS "${XGL_SDK_PREFIX}" NO_DEFAULT_PATH)
include("${CMAKE_CURRENT_LIST_DIR}/VerifySdkPrefix.cmake")
xgl_verify_sdk_prefix("${XGL_SDK_PREFIX}")
get_target_property(sdk_includes xgl::xgl INTERFACE_INCLUDE_DIRECTORIES)
list(LENGTH sdk_includes sdk_include_count)
if(NOT sdk_include_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one public include directory")
endif()
file(
    GLOB installed_headers
    RELATIVE "${sdk_includes}/xgl"
    "${sdk_includes}/xgl/*"
)
list(SORT installed_headers)
set(expected_headers
    xgl.h
    xgl_alloc.h
    xgl_build_config.h
    xgl_config.h
    xgl_diagnostics.h
    xgl_error.h
    xgl_inplace.h
    xgl_runtime.h
    xgl_security.h
    xgl_types.h
)
if(NOT installed_headers STREQUAL expected_headers)
    message(
        FATAL_ERROR
        "Unexpected installed SDK header set: ${installed_headers}"
    )
endif()

add_executable(xgl_consumer main.c)
target_link_libraries(xgl_consumer PRIVATE xgl::xgl)
add_executable(xgl_public_api_consumer public_api.c)
target_link_libraries(xgl_public_api_consumer PRIVATE xgl::xgl)
set_target_properties(
    xgl_consumer
    xgl_public_api_consumer
    PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
)
