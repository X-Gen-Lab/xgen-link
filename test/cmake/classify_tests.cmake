# Executed by CTest after GoogleTest discovery has registered individual cases.
foreach(test_name IN LISTS xgl_discovered_tests)
    set(labels xgl unit)
    if(test_name MATCHES "^(XglIntegrationTest|XglPhyStreamTest|XglNetworkTxContractTest|XglNetworkRoutesContract|XglNetworkMultihopTest)\\.")
        set(labels xgl integration)
    elseif(test_name MATCHES "Properties\\.")
        list(APPEND labels property)
    endif()
    set_tests_properties("${test_name}" PROPERTIES LABELS "${labels}")
endforeach()

if(NOT DEFINED xgl_complete_regression_suite OR xgl_complete_regression_suite)
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/baseline_tests.txt" baseline_tests)
else()
    set(baseline_tests
        XglBoundedProfileTest.ReliableDeliveryUsesExplicitStorage
        XglBoundedProfileTest.RejectsUnsupportedAuthentication
        XglBoundedProfileTest.CapacityShortagePreservesCallerStorage
        XglBoundedProfileTest.RejectsOversizedSingleFrameWithoutTransmit
        XglBoundedProfileTest.ParserRequiresCompleteValidatedFrame)
endif()
foreach(test_name IN LISTS baseline_tests)
    list(FIND xgl_discovered_tests "${test_name}" baseline_index)
    if(baseline_index EQUAL -1)
        message(FATAL_ERROR "Missing historical GoogleTest case: ${test_name}")
    endif()
endforeach()
