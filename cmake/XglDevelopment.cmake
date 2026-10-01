# Explicitly requested host checks, footprint reports and release helpers.
if(XGL_BUILD_NOHEAP_SMOKE)
    add_library(xgl_noheap STATIC ${XGL_SOURCES})
    target_compile_features(xgl_noheap PUBLIC c_std_11)
    target_link_libraries(xgl_noheap PUBLIC xgm::allocator
        PRIVATE ${XGL_PRIVATE_DEPENDENCIES})
    target_compile_options(xgl_noheap PRIVATE ${XGL_C_WARNINGS})
    target_compile_definitions(xgl_noheap PUBLIC XGL_ALLOW_FALLBACK_MALLOC=0)
    target_include_directories(xgl_noheap
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(NOT XGL_PROFILE STREQUAL "boot")
        add_executable(xgl_noheap_smoke tools/noheap_smoke.c)
        target_link_libraries(xgl_noheap_smoke PRIVATE xgl_noheap)
    endif()
    add_executable(xgl_static_workspace_smoke tools/static_workspace_smoke.c)
    target_link_libraries(xgl_static_workspace_smoke PRIVATE xgl_noheap)
    enable_testing()
    add_test(NAME xgl_static_workspace_smoke COMMAND xgl_static_workspace_smoke)
    set_tests_properties(xgl_static_workspace_smoke PROPERTIES
        LABELS integration TIMEOUT 30)
endif()

if(XGL_BUILD_FOOTPRINT_REPORT)
    set(XGL_FOOTPRINT_REPORT ${CMAKE_CURRENT_BINARY_DIR}/footprint/xgl-footprint.txt)
    add_custom_command(
        OUTPUT ${XGL_FOOTPRINT_REPORT}
        COMMAND ${CMAKE_COMMAND}
            -D XGL_TARGET_FILE=$<TARGET_FILE:xgl>
            -D XGL_REPORT_FILE=${XGL_FOOTPRINT_REPORT}
            -D XGL_PROJECT_VERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/footprint_report.cmake
        DEPENDS xgl ${CMAKE_CURRENT_SOURCE_DIR}/tools/footprint_report.cmake
        VERBATIM
    )
    add_custom_target(xgl_footprint DEPENDS ${XGL_FOOTPRINT_REPORT})
endif()

if(XGL_BUILD_STATIC_ANALYSIS_TARGET)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(xgl_static_analysis
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/quality.py
            cppcheck --build-dir ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running the shared static-analysis policy on the actual build"
        VERBATIM
    )
endif()

if(XGL_BUILD_SDK_CONSUMER_SMOKE)
    if(NOT DEFINED XGL_SDK_INSTALL_BINARY_DIR)
        set(XGL_SDK_INSTALL_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    # An SDK built against installed dependencies does not install them again.
    # Preserve their explicit discovery locations for the isolated consumer.
    set(XGL_SDK_DEPENDENCY_HINTS "${CMAKE_CURRENT_BINARY_DIR}/sdk-dependency-hints.cmake")
    file(WRITE "${XGL_SDK_DEPENDENCY_HINTS}"
        "set(XGL_UPSTREAM_PREFIX_PATH [==[${CMAKE_PREFIX_PATH}]==])\n")
    foreach(dependency STATUS BYTES CRC MEMORY CONTAINERS)
        string(TOLOWER "${dependency}" package_suffix)
        set(package_dir "xgen_${package_suffix}_DIR")
        if(DEFINED ${package_dir} AND IS_DIRECTORY "${${package_dir}}")
            file(APPEND "${XGL_SDK_DEPENDENCY_HINTS}"
                "set(${package_dir} [==[${${package_dir}}]==] CACHE PATH \"Explicit dependency package\")\n")
        endif()
    endforeach()
    add_custom_target(xgl_sdk_consumer_smoke
        COMMAND ${CMAKE_COMMAND}
            -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D XGL_INSTALL_BINARY_DIR=${XGL_SDK_INSTALL_BINARY_DIR}
            -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke
            -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
            -D XGL_DEPENDENCY_HINTS=${XGL_SDK_DEPENDENCY_HINTS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk_consumer_smoke.cmake
        DEPENDS xgl ${XGL_SDK_DEPENDENCY_TARGETS}
        COMMENT "Validating installed xgl CMake package with a consumer project"
        VERBATIM
    )
endif()

if(XGL_BUILD_SDK_CONSUMER_SMOKE)
    enable_testing()
    add_test(NAME xgl_sdk_consumer_smoke
        COMMAND ${CMAKE_COMMAND}
            -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D XGL_INSTALL_BINARY_DIR=${XGL_SDK_INSTALL_BINARY_DIR}
            -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke-ctest
            -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
            -D XGL_DEPENDENCY_HINTS=${XGL_SDK_DEPENDENCY_HINTS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk_consumer_smoke.cmake
    )
    set_tests_properties(xgl_sdk_consumer_smoke PROPERTIES
        LABELS integration TIMEOUT 60)
endif()

if(TARGET xgl_noheap_smoke)
    add_test(NAME xgl_noheap_smoke COMMAND xgl_noheap_smoke)
    set_tests_properties(xgl_noheap_smoke PROPERTIES
        LABELS integration TIMEOUT 30)
endif()

if(XGL_BUILD_RELEASE_VALIDATION_TARGET)
    set(XGL_RELEASE_VALIDATION_DEPS xgl)
    if(XGL_BUILD_TESTS)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_tests)
    endif()
    if(XGL_BUILD_FOOTPRINT_REPORT)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_footprint)
    endif()
    if(XGL_BUILD_STATIC_ANALYSIS_TARGET)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_static_analysis)
    endif()
    if(XGL_BUILD_SDK_CONSUMER_SMOKE)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_sdk_consumer_smoke)
    endif()
    if(TARGET xgl_noheap_smoke)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_noheap_smoke)
    endif()
    if(TARGET xgl_static_workspace_smoke)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_static_workspace_smoke)
    endif()

    add_custom_target(xgl_release_validation
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR} --output-on-failure --no-tests=error
        DEPENDS ${XGL_RELEASE_VALIDATION_DEPS}
        COMMENT "Running xgl release validation checks"
        VERBATIM
    )
endif()
