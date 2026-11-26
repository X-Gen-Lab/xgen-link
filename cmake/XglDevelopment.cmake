# Explicitly requested host checks, footprint reports and release helpers.
if(XGL_BUILD_NOHEAP_SMOKE)
    include("${PROJECT_SOURCE_DIR}/test/smoke/Targets.cmake")
endif()

if(XGL_BUILD_FOOTPRINT_REPORT)
    set(archive_report_script "${CMAKE_CURRENT_LIST_DIR}/XglArchiveSizeReport.cmake")
    set(footprint_dir "${CMAKE_CURRENT_BINARY_DIR}/footprint")
    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(multi_config)
        string(APPEND footprint_dir "/$<CONFIG>")
    endif()
    set(XGL_FOOTPRINT_REPORT "${footprint_dir}/xgl-footprint.txt")
    add_custom_command(
        OUTPUT "${XGL_FOOTPRINT_REPORT}"
        COMMAND "${CMAKE_COMMAND}"
            "-DXGL_TARGET_FILE=$<TARGET_FILE:xgl>"
            "-DXGL_REPORT_FILE=${XGL_FOOTPRINT_REPORT}"
            "-DXGL_PROJECT_VERSION=${PROJECT_VERSION}"
            "-DXGL_PROFILE=$<TARGET_PROPERTY:xgl,XGL_PROFILE>"
            "-DXGL_BUILD_CONFIG=$<CONFIG>"
            -P "${archive_report_script}"
        DEPENDS xgl "${archive_report_script}"
        VERBATIM
    )
    add_custom_target(xgl_footprint DEPENDS "${XGL_FOOTPRINT_REPORT}")
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
    add_custom_target(xgl_sdk_consumer_smoke
        COMMAND ${CMAKE_COMMAND}
            -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke/$<CONFIG>
            -D XGL_BUILD_CONFIG=$<CONFIG>
            -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
            -P ${PROJECT_SOURCE_DIR}/test/contracts/sdk/sdk_consumer_smoke.cmake
        DEPENDS xgl
        COMMENT "Validating installed xgl CMake package with a consumer project"
        VERBATIM
    )
endif()

if(XGL_BUILD_SDK_CONSUMER_SMOKE)
    add_test(NAME xgl_sdk_consumer_smoke
        COMMAND ${CMAKE_COMMAND}
            -D XGL_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -D XGL_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-consumer-smoke-ctest/$<CONFIG>
            -D XGL_BUILD_CONFIG=$<CONFIG>
            -D XGL_C_COMPILER=${CMAKE_C_COMPILER}
            -P ${PROJECT_SOURCE_DIR}/test/contracts/sdk/sdk_consumer_smoke.cmake
    )
    set_tests_properties(xgl_sdk_consumer_smoke PROPERTIES
        LABELS integration TIMEOUT 60)
endif()

if(XGL_BUILD_RELEASE_VALIDATION_TARGET)
    if(NOT (XGL_BUILD_TESTS OR XGL_BUILD_NOHEAP_SMOKE OR XGL_BUILD_SDK_CONSUMER_SMOKE))
        message(FATAL_ERROR
            "XGL_BUILD_RELEASE_VALIDATION_TARGET requires host tests, no-heap smoke, or SDK consumer smoke")
    endif()
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
    if(TARGET xgl_noheap_smoke)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_noheap_smoke)
    endif()
    if(TARGET xgl_static_workspace_smoke)
        list(APPEND XGL_RELEASE_VALIDATION_DEPS xgl_static_workspace_smoke)
    endif()

    add_custom_target(xgl_release_validation
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR}
            -C $<CONFIG> --output-on-failure --no-tests=error
        DEPENDS ${XGL_RELEASE_VALIDATION_DEPS}
        COMMENT "Running xgl release validation checks"
        VERBATIM
    )
endif()
