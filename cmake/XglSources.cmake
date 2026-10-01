# Protocol translation units and compile-time feature selection.
set(XGL_SOURCES
    # Public API orchestration
    src/api/xgl_instance.c
    src/api/xgl_instance_links.c
    src/api/xgl_workspace.c
    src/api/xgl_runtime.c
    src/api/xgl_config.c
    src/api/xgl_send.c
    src/api/xgl_send_zerocopy.c
    src/api/xgl_security_session.c
    src/api/xgl_stats.c
    src/api/xgl_version.c

    # Wire format and serialization
    src/wire/xgl_wire_ack_ext.c
    src/wire/xgl_wire_ext.c
    src/wire/xgl_wire.c
    src/wire/xgl_frame.c
    src/wire/xgl_frame_zerocopy.c

    # Security
    src/security/xgl_security_frame.c
    src/security/xgl_security.c

    # Protocol error domain
    src/api/xgl_error.c

    # Data Link Layer
    src/datalink/xgl_parser.c
    src/datalink/xgl_datalink_metadata.c
    src/datalink/xgl_datalink_send.c
    src/datalink/xgl_datalink_receive.c
    src/datalink/xgl_datalink.c

    # Network Layer
    src/network/xgl_route.c
    src/network/xgl_network_send.c
    src/network/xgl_network_receive.c
    src/network/xgl_network_forward.c
    src/network/xgl_network.c

    # Transport Layer
    src/transport/xgl_fragment.c
    src/transport/xgl_fragment_range.c
    src/transport/xgl_fragment_reassembly.c
    src/transport/xgl_reliable.c
    src/transport/xgl_rtt.c
    src/transport/xgl_transport.c
    src/transport/xgl_transport_ack.c
    src/transport/xgl_transport_ack_send.c
    src/transport/xgl_transport_control.c
    src/transport/xgl_transport_peer.c
    src/transport/xgl_transport_runtime.c
    src/transport/xgl_transport_rx.c
    src/transport/xgl_transport_rx_buffer.c
    src/transport/xgl_transport_rx_delivery.c
    src/transport/xgl_transport_tx.c
    src/transport/xgl_transport_tx_message.c
    src/transport/xgl_window.c
)

if(NOT XGL_FEATURE_AUTH)
    list(REMOVE_ITEM XGL_SOURCES src/security/xgl_security.c src/security/xgl_security_frame.c)
endif()
if(NOT XGL_FEATURE_FRAGMENTATION)
    list(FILTER XGL_SOURCES EXCLUDE REGEX "src/transport/xgl_fragment.*\\.c$")
    list(REMOVE_ITEM XGL_SOURCES src/transport/xgl_transport_tx_message.c)
endif()
if(NOT XGL_FEATURE_OUT_OF_ORDER)
    list(REMOVE_ITEM XGL_SOURCES src/transport/xgl_transport_rx_buffer.c)
endif()
