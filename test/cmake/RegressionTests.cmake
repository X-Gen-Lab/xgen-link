# Register tests and labels in discovery order.
include(GoogleTest)
gtest_discover_tests(
    xgl_tests
    DISCOVERY_MODE POST_BUILD
    TEST_LIST xgl_discovered_tests
    PROPERTIES TIMEOUT 30
)
set_property(
    DIRECTORY
    APPEND
    PROPERTY
        TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/profile-tests.cmake"
)
file(
    GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/profile-tests.cmake"
    CONTENT
        "set(xgl_complete_regression_suite ${XGL_FEATURE_AUTH})\ninclude(\"${CMAKE_CURRENT_LIST_DIR}/classify_tests.cmake\")\n"
)

function(xgl_add_regression_target target filter selection description)
    add_custom_target(
        ${target}
        COMMAND
            "${CMAKE_CTEST_COMMAND}" -C $<CONFIG> "${filter}" "${selection}"
            --no-tests=error --output-on-failure
        DEPENDS xgl_tests
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "${description}"
        VERBATIM
    )
endfunction()

xgl_add_regression_target(run_tests -L xgl "Running protocol tests...")
xgl_add_regression_target(run_property_tests -L property "Running property tests...")
xgl_add_regression_target(run_unit_tests -L unit "Running unit tests...")
xgl_add_regression_target(run_architecture_tests -R XglLayeredStatsTest
    "Running layered statistics tests..."
)

add_custom_target(
    test_report
    COMMAND xgl_tests --gtest_output=xml:test_results.xml
    DEPENDS xgl_tests
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating test report..."
    VERBATIM
)
