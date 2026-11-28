set(XGL_TEST_SOURCES support/test_main.cpp)

# Preserve the complete historical suite on profiles with authentication.
if(XGL_FEATURE_AUTH)
    list(
        APPEND XGL_TEST_SOURCES
        unit/api/test_cpp17_config.cpp
        unit/wire/test_coverage_wire.cpp
        unit/api/test_coverage_api.cpp
        integration/test_coverage_delivery.cpp
        unit/api/test_types.cpp
        unit/api/test_instance.cpp
        unit/api/test_config.cpp
        unit/api/test_creation_policy.cpp
        unit/api/test_footprint.cpp
        unit/api/test_send.cpp
        unit/api/test_stats.cpp
        unit/security/test_security.cpp
        integration/test_layered_stats.cpp
        unit/wire/test_frame.cpp
    )
else()
    list(APPEND XGL_TEST_SOURCES unit/api/test_bounded_profile.cpp)
endif()

list(APPEND XGL_TEST_SOURCES unit/wire/test_wire.cpp)
if(XGL_FEATURE_AUTH)
    list(
        APPEND XGL_TEST_SOURCES
        unit/datalink/test_parser.cpp
        unit/datalink/test_datalink.cpp
        unit/network/test_network.cpp
    )
endif()

list(
    APPEND XGL_TEST_SOURCES
    unit/transport/test_rtt.cpp
    unit/transport/test_window.cpp
    unit/transport/test_window_storage.cpp
)

if(XGL_FEATURE_AUTH)
    list(
        APPEND XGL_TEST_SOURCES
        unit/transport/test_reliable.cpp
        unit/transport/test_transport.cpp
        integration/test_integration.cpp
        property/test_memory_properties.cpp
        property/test_frame_properties.cpp
        property/test_network_properties.cpp
        property/test_transport_properties.cpp
        property/test_instance_properties.cpp
        property/test_error_properties.cpp
        property/test_alignment_properties.cpp
        property/test_generator.cpp
    )
else()
    if(XGL_PROFILE STREQUAL "embedded-lite")
        list(APPEND XGL_TEST_SOURCES unit/transport/test_reliable.cpp)
    endif()
    if(XGL_PROFILE STREQUAL "boot")
        list(APPEND XGL_TEST_SOURCES unit/network/test_boot_network.cpp)
    endif()
endif()

# These suites select their supported protocol cases at compile time.
list(
    APPEND XGL_TEST_SOURCES
    unit/api/test_api_contracts.cpp
    unit/api/test_extended_options.cpp
    unit/transport/test_transport_advanced.cpp
    unit/api/test_config_consistency.cpp
    unit/api/test_send_policy.cpp
    unit/datalink/test_datalink_span.cpp
    unit/datalink/test_datalink_resync.cpp
    unit/wire/test_wire_sack_view.cpp
    unit/wire/test_wire_rx_contracts.cpp
    unit/transport/test_transport_sack_view.cpp
    unit/transport/test_transport_rx_contracts.cpp
    unit/transport/test_transport_tx_contracts.cpp
    unit/transport/test_transport_runtime_contracts.cpp
    unit/transport/test_transport_storage.cpp
    unit/transport/test_reliable_owned.cpp
    unit/transport/test_transport_fixed_rto.cpp
    unit/transport/test_timeout_control.cpp
    unit/datalink/test_parser_recovery.cpp
    unit/datalink/test_parser_resync.cpp
    unit/network/test_route.cpp
    unit/network/test_network_contracts.cpp
    integration/test_network_routes_contracts.cpp
    integration/test_transport_admission.cpp
)

if(XGL_FEATURE_STATISTICS)
    list(APPEND XGL_TEST_SOURCES integration/test_network_tx_contracts.cpp)
endif()
if(XGL_FEATURE_FORWARDING AND XGL_FEATURE_AUTH)
    list(APPEND XGL_TEST_SOURCES integration/test_network_multihop.cpp)
endif()

list(APPEND XGL_TEST_SOURCES support/mocks/mock_phy.cpp)
