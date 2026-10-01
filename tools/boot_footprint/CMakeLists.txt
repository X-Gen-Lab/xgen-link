cmake_minimum_required(VERSION 3.24)
project(xgl_boot_footprint LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Use the supplied Cortex-M0 GCC toolchain file")
endif()
get_filename_component(XGL_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(XGL_ARM_SIZE arm-none-eabi-size REQUIRED)
find_program(XGL_ARM_NM arm-none-eabi-nm REQUIRED)
find_program(XGL_ARM_OBJCOPY arm-none-eabi-objcopy REQUIRED)

set(XGL_PROFILE boot CACHE STRING "" FORCE)
set(XGL_ALLOW_FALLBACK_MALLOC OFF CACHE BOOL "" FORCE)
set(XGM_BUILD_LIBC_ALLOCATOR OFF CACHE BOOL "" FORCE)
foreach(prefix XGM XGCT XGB XGCRC XGS)
    set(${prefix}_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endforeach()
foreach(option BUILD_TESTS BUILD_EXAMPLES BUILD_DOCS BUILD_FOOTPRINT_REPORT
        BUILD_STATIC_ANALYSIS_TARGET BUILD_SDK_CONSUMER_SMOKE
        BUILD_RELEASE_VALIDATION_TARGET BUILD_NOHEAP_SMOKE)
    set(XGL_${option} OFF CACHE BOOL "" FORCE)
endforeach()

include("${XGL_SOURCE_ROOT}/dev/Dependencies.cmake")
xgl_dev_provide_components()
add_subdirectory("${XGL_SOURCE_ROOT}" xgen-link)

# This diagnostic object exposes target sizeof values. Its production-source
# inclusion is intentional; it is never linked into the consumer ELF.
add_library(xgl_boot_layout OBJECT layout_probe.c)
target_link_libraries(xgl_boot_layout PRIVATE xgl::xgl
    xgm::pool xgm::size_class xgct::list xgct::bitset
    xgb::bytes xgcrc::crc16)
target_compile_options(xgl_boot_layout PRIVATE -Wall -Wextra -Werror -Wpedantic)
set(layout_binary "${CMAKE_CURRENT_BINARY_DIR}/workspace-layout.bin")
set(layout_header "${CMAKE_CURRENT_BINARY_DIR}/workspace_layout.h")
set(layout_json "${CMAKE_CURRENT_BINARY_DIR}/workspace-layout.json")
add_custom_command(OUTPUT "${layout_binary}" "${layout_header}" "${layout_json}"
    COMMAND "${XGL_ARM_OBJCOPY}"
        --dump-section ".xgl_footprint_layout=${layout_binary}"
        $<TARGET_OBJECTS:xgl_boot_layout>
        "${CMAKE_CURRENT_BINARY_DIR}/layout-inspected.o"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/report.py"
        layout --input "${layout_binary}" --header "${layout_header}"
        --output "${layout_json}"
    DEPENDS xgl_boot_layout $<TARGET_OBJECTS:xgl_boot_layout>
        "${CMAKE_CURRENT_SOURCE_DIR}/report.py"
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(xgl_boot_layout_data DEPENDS "${layout_header}" "${layout_json}")

add_executable(xgl_boot_probe consumer.c startup.S "${layout_header}")
add_dependencies(xgl_boot_probe xgl_boot_layout_data)
set_target_properties(xgl_boot_probe PROPERTIES SUFFIX .elf)
target_include_directories(xgl_boot_probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(xgl_boot_probe PRIVATE xgl::xgl)
target_compile_options(xgl_boot_probe PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_options(xgl_boot_probe PRIVATE
    -nostartfiles --specs=nano.specs --specs=nosys.specs
    "-T${CMAKE_CURRENT_SOURCE_DIR}/memory.ld"
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/xgl_boot_probe.map"
    -Wl,--gc-sections,--print-memory-usage)
set_property(TARGET xgl_boot_probe APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/memory.ld")

add_custom_command(TARGET xgl_boot_probe POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/report.py"
        report --elf $<TARGET_FILE:xgl_boot_probe> --build "${CMAKE_CURRENT_BINARY_DIR}"
        --layout "${layout_json}" --size "${XGL_ARM_SIZE}" --nm "${XGL_ARM_NM}"
        --compiler "${CMAKE_C_COMPILER}" --configuration "${CMAKE_BUILD_TYPE}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/boot-footprint.json"
    VERBATIM)
