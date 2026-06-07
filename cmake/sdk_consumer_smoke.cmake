if(NOT DEFINED XGL_BINARY_DIR)
    message(FATAL_ERROR "XGL_BINARY_DIR is required")
endif()

if(NOT DEFINED XGL_SMOKE_DIR)
    message(FATAL_ERROR "XGL_SMOKE_DIR is required")
endif()

set(install_prefix "${XGL_SMOKE_DIR}/install")
set(consumer_src "${XGL_SMOKE_DIR}/consumer")
set(consumer_build "${XGL_SMOKE_DIR}/consumer-build")
set(consumer_configure_args
    "${CMAKE_COMMAND}" -S "${consumer_src}" -B "${consumer_build}" -G Ninja
    "-DCMAKE_PREFIX_PATH=${install_prefix}"
)

if(DEFINED XGL_C_COMPILER AND NOT XGL_C_COMPILER STREQUAL "")
    list(APPEND consumer_configure_args "-DCMAKE_C_COMPILER=${XGL_C_COMPILER}")
endif()

file(REMOVE_RECURSE "${XGL_SMOKE_DIR}")
file(MAKE_DIRECTORY "${consumer_src}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${XGL_BINARY_DIR}" --prefix "${install_prefix}"
    RESULT_VARIABLE install_result
)

if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "xgl install failed with exit code ${install_result}")
endif()

set(forbidden_installed_headers
    xgl_parser.h
    xgl_reliable.h
    xgl_window.h
    xgl_fragment.h
    xgl_wire.h
    xgl_hashtable.h
)

foreach(header IN LISTS forbidden_installed_headers)
    if(EXISTS "${install_prefix}/include/xgl/${header}")
        message(FATAL_ERROR "internal xgl header was installed: ${header}")
    endif()
endforeach()

file(WRITE "${consumer_src}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.21)
project(xgl_consumer_smoke C)

find_package(xgl CONFIG REQUIRED)

add_executable(xgl_consumer main.c)
target_link_libraries(xgl_consumer PRIVATE xgl::xgl)
]=])

file(WRITE "${consumer_src}/main.c" [=[
#include <xgl/xgl.h>

int main(void) {
    xgl_config_t config;
    xgl_config_get_default(&config);
    return xgl_version_int() >= 10000U ? 0 : 1;
}
]=])

execute_process(
    COMMAND ${consumer_configure_args}
    RESULT_VARIABLE configure_result
)

if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "xgl consumer configure failed with exit code ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
    RESULT_VARIABLE build_result
)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "xgl consumer build failed with exit code ${build_result}")
endif()
