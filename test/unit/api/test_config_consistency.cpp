/**
 * \file            test_config_consistency.cpp
 * \brief           Configuration admission agrees with workspace and lifecycle
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-02
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_host_allocator.h"
#include <gtest/gtest.h>
#include <xgl/xgl.h>

#if XGL_FEATURE_AUTH
#include "test_security_helpers.h"
#include "transport/xgl_transport.h"
#include "wire/xgl_frame.h"
#include "wire/xgl_wire.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {

struct ScopedInstance {
    xgl_handle_t handle = nullptr;

    ~ScopedInstance() {
        xgl_destroy(handle);
    }
};

xgl_config_t bounded_config() {
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    config.memory.allocator = xgl_test_host_allocator();
    return config;
}

struct CountingAllocator {
    xgl_allocator_t service = {};
    size_t attempts = 0U;

    CountingAllocator() {
        service.ctx = this;
        service.alloc = [](void* context, size_t size) -> void* {
            ++static_cast<CountingAllocator*>(context)->attempts;
            return xgl_alloc(xgl_test_host_allocator(), size);
        };
        service.free = [](void*, void* pointer) {
            xgl_free(xgl_test_host_allocator(), pointer);
        };
    }
};

void expect_rejected_before_allocation(xgl_config_t config) {
    CountingAllocator allocator;
    config.memory.allocator = &allocator.service;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    xgl_memory_requirements_t requirements = {};
    requirements.size = 123U;
    EXPECT_EQ(xgl_memory_requirements(&config, &requirements),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(requirements.size, 123U);
    xgl_memory_report_t report = {};
    report.management_bytes = 456U;
    EXPECT_EQ(xgl_memory_report(&config, &report), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(report.management_bytes, 456U);

    ScopedInstance instance;
    instance.handle = xgl_create(&config);
    EXPECT_EQ(instance.handle, nullptr);
    EXPECT_EQ(allocator.attempts, 0U);
}

xgl_error_t fixed_tag_sign(const xgl_auth_input_t*, uint8_t* tag,
                           size_t capacity, size_t* length, void*) {
    if (capacity < XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    std::memset(tag, 0x5A, XGL_AUTH_TAG_MAX_LEN);
    *length = XGL_AUTH_TAG_MAX_LEN;
    return XGL_OK;
}

xgl_error_t fixed_tag_verify(const xgl_auth_input_t*, const uint8_t*, size_t,
                             bool* valid, void*) {
    *valid = true;
    return XGL_OK;
}

xgl_auth_provider_t fixed_tag_provider() {
    xgl_auth_provider_t provider = {};
    provider.sign = fixed_tag_sign;
    provider.verify = fixed_tag_verify;
    provider.tag_len = XGL_AUTH_TAG_MAX_LEN;
    return provider;
}

struct MandatoryFrames {
    size_t data = 0U;
    size_t hello = 0U;
    size_t ack = 0U;

    size_t maximum() const {
        return std::max({data, hello, ack});
    }
};

bool measure_mandatory_frames(size_t tag_len, MandatoryFrames* measured) {
    xgl_frame_layout_t layout = {};
    if (xgl_frame_measure_lengths(1U, 0U, tag_len, &layout) != XGL_OK) {
        return false;
    }
    measured->data = layout.frame_len;

    uint8_t extensions[32] = {};
    size_t extensions_len = 0U;
    const uint8_t hello_type = XGL_TRANSPORT_CONTROL_HELLO;
    if (xgl_wire_encode_ext(extensions, sizeof(extensions),
                            XGL_WIRE_EXT_DATA_TYPE, &hello_type,
                            sizeof(hello_type), &extensions_len) != XGL_OK ||
        xgl_frame_measure_lengths(0U, extensions_len, tag_len, &layout) !=
            XGL_OK) {
        return false;
    }
    measured->hello = layout.frame_len;

    uint8_t ack_value[32] = {};
    size_t ack_value_len = 0U;
    const xgl_wire_ack_range_t range = {0U, 1U};
    if (xgl_wire_encode_ack_range_ext_value(ack_value, sizeof(ack_value), 1U,
                                            0U, &range, 1U,
                                            &ack_value_len) != XGL_OK ||
        xgl_wire_encode_ext(extensions, sizeof(extensions),
                            XGL_WIRE_EXT_ACK_RANGE, ack_value, ack_value_len,
                            &extensions_len) != XGL_OK ||
        xgl_frame_measure_lengths(0U, extensions_len, tag_len, &layout) !=
            XGL_OK) {
        return false;
    }
    measured->ack = layout.frame_len;
    return true;
}

struct ConnectedPhy {
    ConnectedPhy* remote = nullptr;
    std::vector<uint8_t> incoming;
    std::vector<size_t> transmitted;
    size_t received_payloads = 0U;
};

xgl_phy_ops_t connected_phy(ConnectedPhy* state) {
    xgl_phy_ops_t phy = {};
    phy.user_data = state;
    phy.tx = [](const uint8_t* bytes, size_t length, void* context) {
        auto& port = *static_cast<ConnectedPhy*>(context);
        port.transmitted.push_back(length);
        if (port.remote != nullptr) {
            port.remote->incoming.insert(port.remote->incoming.end(), bytes,
                                         bytes + length);
        }
        return XGL_OK;
    };
    phy.rx = [](uint8_t* bytes, size_t* length, void* context) {
        auto& port = *static_cast<ConnectedPhy*>(context);
        *length = std::min(*length, port.incoming.size());
        std::copy_n(port.incoming.begin(), *length, bytes);
        port.incoming.erase(port.incoming.begin(),
                            port.incoming.begin() +
                                static_cast<std::ptrdiff_t>(*length));
        return XGL_OK;
    };
    return phy;
}
} /* namespace */

TEST(XglConfigConsistency, OptionalInvalidProviderIsRejectedBeforeAllocation) {
    for (unsigned invalid = 0U; invalid < 4U; ++invalid) {
        SCOPED_TRACE(invalid);
        auto provider = fixed_tag_provider();
        if (invalid == 0U) {
            provider.sign = nullptr;
        } else if (invalid == 1U) {
            provider.verify = nullptr;
        } else if (invalid == 2U) {
            provider.tag_len = 0U;
        } else {
            provider.tag_len = XGL_AUTH_TAG_MAX_LEN + 1U;
        }
        auto config = bounded_config();
        config.auth_provider = &provider;
        ASSERT_FALSE(config.auth_required);
        expect_rejected_before_allocation(config);
    }
}

TEST(XglConfigConsistency, OptionalProviderDoesNotImposeAuthenticationBudget) {
    auto provider = fixed_tag_provider();
    auto config = bounded_config();
    config.auth_provider = &provider;
    config.protocol.max_frame_size = 64U;
    config.memory.rx_buffer_size = 64U;
    ConnectedPhy port;
    auto phy = connected_phy(&port);
    xgl_route_item_t route = {2U, &phy, 64U, 100U, 0U};
    config.route_table = &route;
    config.route_table_len = 1U;
    ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
    ScopedInstance instance;
    instance.handle = xgl_create(&config);
    ASSERT_NE(instance.handle, nullptr);
    ASSERT_EQ(xgl_init(instance.handle), XGL_OK);

    const uint8_t byte = 0xA5U;
    xgl_tx_data_t tx = {};
    tx.target_id = 2U;
    tx.data = &byte;
    tx.data_len = 1U;
    EXPECT_EQ(xgl_send_at(instance.handle, &tx, 0U), XGL_OK);
    ASSERT_EQ(port.transmitted.size(), 1U);
    EXPECT_LE(port.transmitted.front(), 64U);
}

TEST(XglConfigConsistency,
     MandatoryAuthenticationRejectsFrame64BeforeAllocation) {
    auto provider = fixed_tag_provider();
    auto config = bounded_config();
    config.auth_required = true;
    config.auth_provider = &provider;
    config.protocol.max_frame_size = 64U;
    config.memory.rx_buffer_size = 64U;
    ConnectedPhy port;
    auto phy = connected_phy(&port);
    xgl_route_item_t route = {2U, &phy, 64U, 100U, 0U};
    config.route_table = &route;
    config.route_table_len = 1U;
    expect_rejected_before_allocation(config);
}

TEST(XglConfigConsistency,
     MandatoryAuthenticationChecksBaseControlAndDataBudget) {
    auto provider = fixed_tag_provider();
    MandatoryFrames measured;
    ASSERT_TRUE(measure_mandatory_frames(provider.tag_len, &measured));
    for (size_t too_small :
         {measured.data - 1U, measured.hello - 1U, measured.ack - 1U}) {
        SCOPED_TRACE(too_small);
        auto config = bounded_config();
        config.auth_required = true;
        config.auth_provider = &provider;
        config.protocol.max_frame_size = static_cast<uint16_t>(too_small);
        expect_rejected_before_allocation(config);
    }
}

TEST(XglConfigConsistency, SmallRouteKeepsForwardingAndChecksActualSendBudget) {
    auto provider = fixed_tag_provider();
    auto config = bounded_config();
    config.auth_required = true;
    config.auth_provider = &provider;
    ConnectedPhy port;
    auto phy = connected_phy(&port);
    xgl_route_item_t routes[] = {{2U, &phy, 64U, 100U, 0U},
                                 {3U, &phy, 128U, 100U, 0U}};
    config.route_table = routes;
    config.route_table_len = 2U;
    ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
    ScopedInstance instance;
    instance.handle = xgl_create(&config);
    ASSERT_NE(instance.handle, nullptr);
    ASSERT_EQ(xgl_init(instance.handle), XGL_OK);
    const auto session = test_session_config(2U);
    ASSERT_EQ(xgl_install_security_session(instance.handle, &session), XGL_OK);

    const uint8_t byte = 0xA5U;
    xgl_tx_data_t tx = {};
    tx.target_id = 2U;
    tx.data = &byte;
    tx.data_len = 1U;
    EXPECT_EQ(xgl_send_at(instance.handle, &tx, 0U), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_TRUE(port.transmitted.empty());

#if XGL_FEATURE_FORWARDING
    xgl_frame_params_t params = {};
    params.source_id = 3U;
    params.target_id = 2U;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.payload = &byte;
    params.payload_len = 1U;
    params.ttl = 8U;
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    uint8_t serialized[128] = {};
    size_t length = 0U;
    ASSERT_EQ(
        xgl_frame_serialize(serialized, sizeof(serialized), &frame, &length),
        XGL_OK);
    ASSERT_LE(length, routes[0].max_frame_size);
    port.incoming.assign(serialized, serialized + length);
    const xgl_work_budget_t budget = {128U, 100U};
    ASSERT_EQ(xgl_step(instance.handle, 0U, &budget), XGL_OK);
    ASSERT_EQ(port.transmitted.size(), 1U);
    EXPECT_EQ(port.transmitted.front(), length);
#endif
}

TEST(XglConfigConsistency,
     ExactBaseAuthenticationBudgetSupportsReliableExchange) {
    auto provider = fixed_tag_provider();
    MandatoryFrames measured;
    ASSERT_TRUE(measure_mandatory_frames(provider.tag_len, &measured));
    const auto mtu = static_cast<uint16_t>(measured.maximum());
    ConnectedPhy sender_port;
    ConnectedPhy receiver_port;
    sender_port.remote = &receiver_port;
    receiver_port.remote = &sender_port;
    auto sender_phy = connected_phy(&sender_port);
    auto receiver_phy = connected_phy(&receiver_port);
    xgl_route_item_t sender_route = {2U, &sender_phy, mtu, 100U, 0U};
    xgl_route_item_t receiver_route = {1U, &receiver_phy, mtu, 100U, 0U};
    auto sender_config = bounded_config();
    sender_config.auth_required = true;
    sender_config.auth_provider = &provider;
    sender_config.protocol.max_frame_size = mtu;
    sender_config.route_table = &sender_route;
    sender_config.route_table_len = 1U;
    auto receiver_config = sender_config;
    receiver_config.source_id = 2U;
    receiver_config.route_table = &receiver_route;
    receiver_config.callback_user_data = &receiver_port;
    receiver_config.rx_callback = [](xgl_handle_t, uint16_t, uint8_t,
                                     const uint8_t*, size_t, void* context) {
        ++static_cast<ConnectedPhy*>(context)->received_payloads;
    };
    ASSERT_EQ(xgl_config_validate(&sender_config), XGL_OK);
    ASSERT_EQ(xgl_config_validate(&receiver_config), XGL_OK);
    ScopedInstance sender;
    ScopedInstance receiver;
    sender.handle = xgl_create(&sender_config);
    receiver.handle = xgl_create(&receiver_config);
    ASSERT_NE(sender.handle, nullptr);
    ASSERT_NE(receiver.handle, nullptr);
    ASSERT_EQ(xgl_init(sender.handle), XGL_OK);
    ASSERT_EQ(xgl_init(receiver.handle), XGL_OK);
    const auto sender_session = test_session_config(2U);
    const auto receiver_session = test_session_config(1U, 0U, 0U, 7U, true);
    ASSERT_EQ(xgl_install_security_session(sender.handle, &sender_session),
              XGL_OK);
    ASSERT_EQ(xgl_install_security_session(receiver.handle, &receiver_session),
              XGL_OK);

    const uint8_t byte = 0xA5U;
    xgl_tx_data_t tx = {};
    tx.target_id = 2U;
    tx.data = &byte;
    tx.data_len = 1U;
    tx.reliable = true;
    ASSERT_EQ(xgl_send_at(sender.handle, &tx, 0U), XGL_OK);
    const xgl_work_budget_t budget = {128U, 100U};
    ASSERT_EQ(xgl_step(receiver.handle, 0U, &budget), XGL_OK);
    ASSERT_EQ(xgl_step(receiver.handle, 10U, &budget), XGL_OK);
    ASSERT_EQ(xgl_step(sender.handle, 20U, &budget), XGL_OK);
    EXPECT_EQ(receiver_port.received_payloads, 1U);
    ASSERT_EQ(receiver_port.transmitted.size(), 1U);
    EXPECT_EQ(receiver_port.transmitted.front(), measured.ack);
    for (size_t length : sender_port.transmitted) {
        EXPECT_LE(length, measured.maximum());
    }
    EXPECT_EQ(xgl_send_at(sender.handle, &tx, 21U), XGL_OK);
}
#endif
