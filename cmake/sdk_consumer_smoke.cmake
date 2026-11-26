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
    "-DXGL_SDK_PREFIX=${install_prefix}"
)
set(configuration_args)
if(DEFINED XGL_BUILD_CONFIG AND NOT XGL_BUILD_CONFIG STREQUAL "")
    list(APPEND configuration_args --config "${XGL_BUILD_CONFIG}")
    list(APPEND consumer_configure_args "-DCMAKE_BUILD_TYPE=${XGL_BUILD_CONFIG}")
endif()
if(DEFINED XGL_C_COMPILER AND NOT XGL_C_COMPILER STREQUAL "")
    list(APPEND consumer_configure_args "-DCMAKE_C_COMPILER=${XGL_C_COMPILER}")
endif()

file(REAL_PATH "${XGL_BINARY_DIR}" binary_root)
file(REAL_PATH "${XGL_SMOKE_DIR}" smoke_root)
cmake_path(IS_PREFIX binary_root "${smoke_root}" NORMALIZE smoke_is_child)
if(NOT smoke_is_child OR smoke_root STREQUAL binary_root)
    message(FATAL_ERROR "SDK smoke output must be a child of the build directory")
endif()
file(REMOVE_RECURSE "${smoke_root}")
file(MAKE_DIRECTORY "${consumer_src}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${binary_root}" ${configuration_args}
        --prefix "${smoke_root}/install-stage"
    RESULT_VARIABLE install_result
)

if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "xgl install failed with exit code ${install_result}")
endif()

file(RENAME "${smoke_root}/install-stage" "${install_prefix}")

file(GLOB_RECURSE sdk_configs LIST_DIRECTORIES FALSE "${install_prefix}/xglConfig.cmake")
list(LENGTH sdk_configs sdk_config_count)
if(NOT sdk_config_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one xglConfig.cmake in the installed SDK")
endif()
get_filename_component(sdk_config_dir "${sdk_configs}" DIRECTORY)
list(APPEND consumer_configure_args "-Dxgl_DIR=${sdk_config_dir}")

file(WRITE "${consumer_src}/CMakeLists.txt" [=[
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
file(GLOB installed_headers RELATIVE "${sdk_includes}/xgl" "${sdk_includes}/xgl/*")
list(SORT installed_headers)
if(NOT installed_headers STREQUAL "xgl.h;xgl_build_config.h;xgl_config.h;xgl_error.h;xgl_types.h")
    message(FATAL_ERROR "Unexpected installed SDK header set: ${installed_headers}")
endif()

add_executable(xgl_consumer main.c)
target_link_libraries(xgl_consumer PRIVATE xgl::xgl)
add_executable(xgl_public_api_consumer public_api.c)
target_link_libraries(xgl_public_api_consumer PRIVATE xgl::xgl)
set_target_properties(xgl_consumer xgl_public_api_consumer PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
]=])

configure_file("${CMAKE_CURRENT_LIST_DIR}/../tools/static_workspace_smoke.c"
    "${consumer_src}/main.c" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../tools/public_api_smoke.c"
    "${consumer_src}/public_api.c" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../test/cmake/VerifySdkPrefix.cmake"
    "${consumer_src}/VerifySdkPrefix.cmake" COPYONLY)

execute_process(
    COMMAND ${consumer_configure_args}
    RESULT_VARIABLE configure_result
)

if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "xgl consumer configure failed with exit code ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" ${configuration_args}
    RESULT_VARIABLE build_result
)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "xgl consumer build failed with exit code ${build_result}")
endif()

foreach(consumer IN ITEMS xgl_consumer xgl_public_api_consumer)
    if(CMAKE_HOST_WIN32)
        set(consumer_executable "${consumer_build}/${consumer}.exe")
    else()
        set(consumer_executable "${consumer_build}/${consumer}")
    endif()
    execute_process(COMMAND "${consumer_executable}" RESULT_VARIABLE run_result)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "Installed SDK ${consumer} failed: ${run_result}")
    endif()
endforeach()
