if(NOT DEFINED XGL_BINARY_DIR)
    message(FATAL_ERROR "XGL_BINARY_DIR is required")
endif()

if(NOT DEFINED XGL_SMOKE_DIR)
    message(FATAL_ERROR "XGL_SMOKE_DIR is required")
endif()

set(install_prefix "${XGL_SMOKE_DIR}/install")
set(consumer_src "${XGL_SMOKE_DIR}/consumer")
set(consumer_build "${XGL_SMOKE_DIR}/consumer-build")
if(DEFINED XGL_DEPENDENCY_HINTS)
    include("${XGL_DEPENDENCY_HINTS}")
endif()
set(consumer_prefixes "${install_prefix}" ${XGL_UPSTREAM_PREFIX_PATH})
string(REPLACE ";" "\\;" consumer_prefixes "${consumer_prefixes}")
set(consumer_configure_args
    "${CMAKE_COMMAND}" -S "${consumer_src}" -B "${consumer_build}" -G Ninja
    "-DCMAKE_PREFIX_PATH=${consumer_prefixes}"
)
if(DEFINED XGL_DEPENDENCY_HINTS)
    list(APPEND consumer_configure_args -C "${XGL_DEPENDENCY_HINTS}")
endif()

if(DEFINED XGL_C_COMPILER AND NOT XGL_C_COMPILER STREQUAL "")
    list(APPEND consumer_configure_args "-DCMAKE_C_COMPILER=${XGL_C_COMPILER}")
endif()

file(REAL_PATH "${XGL_BINARY_DIR}" binary_root)
if(NOT DEFINED XGL_INSTALL_BINARY_DIR)
    set(XGL_INSTALL_BINARY_DIR "${XGL_BINARY_DIR}")
endif()
file(REAL_PATH "${XGL_INSTALL_BINARY_DIR}" install_root)
cmake_path(IS_PREFIX install_root "${binary_root}" NORMALIZE install_contains_binary)
if(NOT install_contains_binary)
    message(FATAL_ERROR "SDK installation root must contain the link build directory")
endif()
file(REAL_PATH "${XGL_SMOKE_DIR}" smoke_root)
cmake_path(IS_PREFIX binary_root "${smoke_root}" NORMALIZE smoke_is_child)
if(NOT smoke_is_child OR smoke_root STREQUAL binary_root)
    message(FATAL_ERROR "SDK smoke output must be a child of the build directory")
endif()
file(REMOVE_RECURSE "${smoke_root}")
file(MAKE_DIRECTORY "${consumer_src}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${install_root}" --prefix "${install_prefix}"
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

if(EXISTS "${install_prefix}/include/xgl/internal")
    message(FATAL_ERROR "internal xgl header directory was installed")
endif()

file(WRITE "${consumer_src}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.21)
project(xgl_consumer_smoke C)

find_package(xgl CONFIG REQUIRED)

add_executable(xgl_consumer main.c)
target_link_libraries(xgl_consumer PRIVATE xgl::xgl)
]=])

configure_file("${CMAKE_CURRENT_LIST_DIR}/../tools/static_workspace_smoke.c"
    "${consumer_src}/main.c" COPYONLY)

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

if(CMAKE_HOST_WIN32)
    set(consumer_executable "${consumer_build}/xgl_consumer.exe")
else()
    set(consumer_executable "${consumer_build}/xgl_consumer")
endif()
execute_process(COMMAND "${consumer_executable}" RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Installed SDK lifecycle test failed: ${run_result}")
endif()
