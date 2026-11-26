cmake_minimum_required(VERSION 3.24)
project(xgl_boot_footprint LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Use the supplied Cortex-M0 GCC toolchain file")
endif()
get_filename_component(XGL_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

option(XGL_FOOTPRINT_LTO "Optimize protocol across translation units" OFF)
option(XGL_FOOTPRINT_ADVANCED_API "Measure explicitly exercised advanced runtime APIs" OFF)
if(XGL_FOOTPRINT_LTO)
    include(CheckIPOSupported)
    function(xgl_footprint_check_ipo)
        # CMake links a hosted test executable. Supply the same bare-metal
        # runtime specs in this local check without altering production flags.
        string(APPEND CMAKE_C_FLAGS " --specs=nano.specs --specs=nosys.specs")
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES C)
        if(NOT ipo_supported)
            message(FATAL_ERROR "XGL_FOOTPRINT_LTO requires working C IPO support: ${ipo_error}")
        endif()
    endfunction()
    xgl_footprint_check_ipo()
endif()
# This diagnostic owns its optimization mode; production consumers select theirs.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION "${XGL_FOOTPRINT_LTO}")

set(XGL_FOOTPRINT_FRAME_SIZE 128 CACHE STRING "Probe frame MTU in bytes")
set(XGL_FOOTPRINT_RX_SIZE 128 CACHE STRING "Probe per-link RX storage in bytes")
foreach(capacity XGL_FOOTPRINT_FRAME_SIZE XGL_FOOTPRINT_RX_SIZE)
    if(NOT "${${capacity}}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${capacity} must be a decimal byte count")
    endif()
endforeach()
if(XGL_FOOTPRINT_FRAME_SIZE LESS 64 OR XGL_FOOTPRINT_FRAME_SIZE GREATER 2048 OR
   XGL_FOOTPRINT_RX_SIZE LESS XGL_FOOTPRINT_FRAME_SIZE OR XGL_FOOTPRINT_RX_SIZE GREATER 4096)
    message(FATAL_ERROR "Probe requires MTU 64..2048 and RX capacity MTU..4096")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(XGL_ARM_SIZE arm-none-eabi-size REQUIRED)
find_program(XGL_ARM_NM arm-none-eabi-nm REQUIRED)
find_program(XGL_ARM_OBJCOPY arm-none-eabi-objcopy REQUIRED)

set(XGL_PROFILE boot CACHE STRING "" FORCE)
set(XGL_ALLOW_FALLBACK_MALLOC OFF CACHE BOOL "" FORCE)
foreach(option BUILD_TESTS BUILD_EXAMPLES BUILD_DOCS BUILD_FOOTPRINT_REPORT
        BUILD_STATIC_ANALYSIS_TARGET BUILD_SDK_CONSUMER_SMOKE
        BUILD_RELEASE_VALIDATION_TARGET BUILD_NOHEAP_SMOKE)
    set(XGL_${option} OFF CACHE BOOL "" FORCE)
endforeach()

add_subdirectory("${XGL_SOURCE_ROOT}" xgen-link)
if(XGL_FOOTPRINT_ADVANCED_API AND NOT XGL_ENABLE_ADVANCED_RUNTIME)
    message(FATAL_ERROR "XGL_FOOTPRINT_ADVANCED_API requires XGL_ENABLE_ADVANCED_RUNTIME=ON")
endif()

# The diagnostic object measures private target types without compiling or
# linking another copy of the protocol implementation.
add_library(xgl_boot_layout OBJECT layout_probe.c)
set_property(TARGET xgl_boot_layout PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
target_include_directories(xgl_boot_layout PRIVATE "${XGL_SOURCE_ROOT}/src")
target_link_libraries(xgl_boot_layout PRIVATE xgl::xgl)
target_compile_options(xgl_boot_layout PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_compile_definitions(xgl_boot_layout PRIVATE
    XGL_FOOTPRINT_FRAME_SIZE=${XGL_FOOTPRINT_FRAME_SIZE}U
    XGL_FOOTPRINT_RX_SIZE=${XGL_FOOTPRINT_RX_SIZE}U)
set(layout_binary "${CMAKE_CURRENT_BINARY_DIR}/workspace-layout.bin")
set(layout_header "${CMAKE_CURRENT_BINARY_DIR}/workspace_layout.h")
set(layout_json "${CMAKE_CURRENT_BINARY_DIR}/workspace-layout.json")
set(report_script "${CMAKE_CURRENT_SOURCE_DIR}/report.py")
add_custom_command(OUTPUT "${layout_binary}" "${layout_header}" "${layout_json}"
    COMMAND "${XGL_ARM_OBJCOPY}"
        --dump-section ".xgl_footprint_layout=${layout_binary}"
        $<TARGET_OBJECTS:xgl_boot_layout>
        "${CMAKE_CURRENT_BINARY_DIR}/layout-inspected.o"
    COMMAND "${Python3_EXECUTABLE}" "${report_script}"
        layout --input "${layout_binary}" --header "${layout_header}"
        --output "${layout_json}"
    DEPENDS xgl_boot_layout $<TARGET_OBJECTS:xgl_boot_layout> "${report_script}"
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(xgl_boot_layout_data DEPENDS "${layout_header}" "${layout_json}")

add_executable(xgl_boot_probe_elf consumer.c startup.S "${layout_header}")
add_dependencies(xgl_boot_probe_elf xgl_boot_layout_data)
set_target_properties(xgl_boot_probe_elf PROPERTIES
    OUTPUT_NAME xgl_boot_probe
    SUFFIX .elf
    INTERPROCEDURAL_OPTIMIZATION FALSE)
target_include_directories(xgl_boot_probe_elf PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(xgl_boot_probe_elf PRIVATE xgl::xgl)
target_compile_options(xgl_boot_probe_elf PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_compile_definitions(xgl_boot_probe_elf PRIVATE
    XGL_FOOTPRINT_ADVANCED_API=$<BOOL:${XGL_FOOTPRINT_ADVANCED_API}>
    XGL_FOOTPRINT_FRAME_SIZE=${XGL_FOOTPRINT_FRAME_SIZE}U
    XGL_FOOTPRINT_RX_SIZE=${XGL_FOOTPRINT_RX_SIZE}U)
target_link_options(xgl_boot_probe_elf PRIVATE
    -nostartfiles --specs=nano.specs --specs=nosys.specs
    "-T${CMAKE_CURRENT_SOURCE_DIR}/memory.ld"
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/xgl_boot_probe.map"
    -Wl,--gc-sections,--print-memory-usage)
set_property(TARGET xgl_boot_probe_elf APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/memory.ld")

# Keep target layout and the external PHY callbacks opaque to whole-program
# analysis; only library objects and the final link participate in LTO.
set(footprint_ipo_compile_options "[]")
set(footprint_ipo_link_options "[]")
if(XGL_FOOTPRINT_LTO)
    target_link_options(xgl_boot_probe_elf PRIVATE -flto)
    string(JOIN "\", \"" footprint_ipo_flags ${CMAKE_C_COMPILE_OPTIONS_IPO})
    set(footprint_ipo_compile_options "[\"${footprint_ipo_flags}\"]")
    set(footprint_ipo_link_options "[\"-flto\"]")
endif()
set(optimization_json "${CMAKE_CURRENT_BINARY_DIR}/footprint-optimization.json")
file(GENERATE OUTPUT "${optimization_json}"
    CONTENT "{\n\
  \"enabled\": $<IF:$<BOOL:${XGL_FOOTPRINT_LTO}>,true,false>,\n\
  \"library_ipo\": $<IF:$<BOOL:$<TARGET_PROPERTY:xgl,INTERPROCEDURAL_OPTIMIZATION>>,true,false>,\n\
  \"layout_ipo\": $<IF:$<BOOL:$<TARGET_PROPERTY:xgl_boot_layout,INTERPROCEDURAL_OPTIMIZATION>>,true,false>,\n\
  \"consumer_ipo\": $<IF:$<BOOL:$<TARGET_PROPERTY:xgl_boot_probe_elf,INTERPROCEDURAL_OPTIMIZATION>>,true,false>,\n\
  \"compile_options\": ${footprint_ipo_compile_options},\n\
  \"link_options\": ${footprint_ipo_link_options}\n\
}\n")

# Keep the public build target and measure the linked ELF through separate outputs.
set(report_stamp "${CMAKE_CURRENT_BINARY_DIR}/boot-footprint.stamp")
set(report_json "${CMAKE_CURRENT_BINARY_DIR}/boot-footprint.json")
set(report_text "${CMAKE_CURRENT_BINARY_DIR}/boot-footprint.txt")
# The writer preserves diagnostics on failure. Only a successful audit marks
# those outputs up to date.
add_custom_command(OUTPUT "${report_stamp}" "${report_json}" "${report_text}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${report_stamp}"
    COMMAND "${Python3_EXECUTABLE}" "${report_script}"
        report --elf "$<TARGET_FILE:xgl_boot_probe_elf>" --build "${CMAKE_CURRENT_BINARY_DIR}"
        --layout "${layout_json}" --size "${XGL_ARM_SIZE}" --nm "${XGL_ARM_NM}"
        --compiler "${CMAKE_C_COMPILER}" --configuration "${CMAKE_BUILD_TYPE}"
        --output "${report_json}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${report_stamp}"
    DEPENDS xgl_boot_probe_elf "${layout_json}" "${optimization_json}"
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt" "${report_script}"
    VERBATIM)
add_custom_target(xgl_boot_probe ALL DEPENDS "${report_stamp}" "${report_json}" "${report_text}")
