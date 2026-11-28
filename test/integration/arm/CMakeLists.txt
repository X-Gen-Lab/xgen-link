cmake_minimum_required(VERSION 3.24)
project(xgl_arm_consumer LANGUAGES C)

find_package(
    xgl
    0.0.1
    CONFIG
    REQUIRED
    COMPONENTS wire reliable
    PATHS "${XGL_SDK_PREFIX}"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
include(../../contracts/sdk/VerifySdkPrefix.cmake)
xgl_verify_sdk_prefix("${XGL_SDK_PREFIX}")
if(NOT xgl_PROFILE STREQUAL "boot")
    message(FATAL_ERROR "The Cortex-M0 reference requires the Boot SDK")
endif()

set(XGL_REFERENCE_WORKSPACE_BYTES 2048 CACHE STRING "Static workspace bytes")
add_executable(xgl_arm_consumer consumer.c)
set_target_properties(
    xgl_arm_consumer
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        SUFFIX .elf
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/memory.ld"
)
target_compile_definitions(
    xgl_arm_consumer
    PRIVATE XGL_REFERENCE_WORKSPACE_BYTES=${XGL_REFERENCE_WORKSPACE_BYTES}
)
target_compile_options(
    xgl_arm_consumer
    PRIVATE -Wall -Wextra -Werror -Wpedantic
)
target_link_libraries(xgl_arm_consumer PRIVATE xgl::xgl)
target_link_options(
    xgl_arm_consumer
    PRIVATE
        -nostartfiles
        --specs=nano.specs
        --specs=nosys.specs
        "-T${CMAKE_CURRENT_SOURCE_DIR}/memory.ld"
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/xgl_arm_consumer.map"
        -Wl,--gc-sections,--print-memory-usage
)
