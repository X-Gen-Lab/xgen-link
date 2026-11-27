/**
 * \file            test_network_multihop.cpp
 * \brief           Real-instance routing, endpoint trust and failure recovery
 * \author          X-Gen Lab
 */

#include "test_host_allocator.h"
#include "test_security_helpers.h"

#include <api/xgl_instance_internal.h>
#include <mock_phy.h>
#include <network/xgl_network.h>
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>
#include <wire/xgl_wire_bytes.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

Bytes copy_bytes(const uint8_t* data, size_t length) {
    return length == 0U ? Bytes{} : Bytes(data, data + length);
}

/** \brief           Record synchronous PHY submissions without parsing them. */
struct ObservedPhy {
    xgl_phy_ops_t delegate = {};
    xgl_phy_ops_t ops = {transmit, receive, this};
    xgl_error_t next_error = XGL_OK;
    size_t attempts = 0U;
    std::vector<Bytes> frames;

    static xgl_error_t transmit(const uint8_t* data, size_t length,
                                void* user) {
        auto& self = *static_cast<ObservedPhy*>(user);
        ++self.attempts;
        const xgl_error_t result = self.next_error;
        self.next_error = XGL_OK;
        if (result != XGL_OK) {
            return result;
        }
        const xgl_error_t submitted =
            self.delegate.tx(data, length, self.delegate.user_data);
        if (submitted == XGL_OK) {
            self.frames.emplace_back(data, data + length);
        }
        return submitted;
    }

    static xgl_error_t receive(uint8_t* data, size_t* length, void* user) {
        auto& self = *static_cast<ObservedPhy*>(user);
        return self.delegate.rx(data, length, self.delegate.user_data);
    }
};

/**
 * \brief           Test-only provider recording exact authentication inputs
 * \note            Tags index recorded inputs; this is not cryptography.
 */
struct AuthenticationLedger {
    struct Record {
        xgl_auth_input_t fields;
        Bytes aad;
        Bytes payload;
    };

    std::vector<Record> signed_inputs;
    std::array<size_t, 4> verified_by_target = {};
    xgl_auth_provider_t provider = {sign, verify, 4U, this};

    static xgl_error_t sign(const xgl_auth_input_t* input, uint8_t* tag,
                            size_t capacity, size_t* length, void* user) {
        auto& self = *static_cast<AuthenticationLedger*>(user);
        const size_t tag_size = self.provider.tag_len;
        if (capacity < tag_size || tag_size < 4U) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        self.signed_inputs.push_back(
            {*input, copy_bytes(input->aad, input->aad_len),
             copy_bytes(input->payload, input->payload_len)});
        self.signed_inputs.back().fields.aad = nullptr;
        self.signed_inputs.back().fields.payload = nullptr;
        xgl_serialize_u32_le(tag,
                             static_cast<uint32_t>(self.signed_inputs.size()));
        std::memset(tag + 4U, 0, tag_size - 4U);
        *length = tag_size;
        return XGL_OK;
    }

    static xgl_error_t verify(const xgl_auth_input_t* input, const uint8_t* tag,
                              size_t length, bool* valid, void* user) {
        auto& self = *static_cast<AuthenticationLedger*>(user);
        if (input->target_id < self.verified_by_target.size()) {
            ++self.verified_by_target[input->target_id];
        }
        *valid = false;
        if (length != self.provider.tag_len || length < 4U ||
            !std::all_of(
                tag + 4U, tag + length,
                [](uint8_t byte) { return byte == 0U; })) {
            return XGL_OK;
        }
        const uint32_t id = xgl_deserialize_u32_le(tag);
        if (id == 0U || id > self.signed_inputs.size()) {
            return XGL_OK;
        }
        const Record& record = self.signed_inputs[id - 1U];
        const auto& expected = record.fields;
        *valid =
            input->version == expected.version &&
            input->wire_version == expected.wire_version &&
            input->source_id == expected.source_id &&
            input->target_id == expected.target_id &&
            input->connection_id == expected.connection_id &&
            input->session_epoch == expected.session_epoch &&
            input->key_id == expected.key_id &&
            input->security_seq == expected.security_seq &&
            std::memcmp(input->nonce, expected.nonce, XGL_AUTH_NONCE_SIZE) ==
                0 &&
            copy_bytes(input->aad, input->aad_len) == record.aad &&
            copy_bytes(input->payload, input->payload_len) == record.payload;
        return XGL_OK;
    }
};

struct Node {
    struct Delivery {
        uint16_t source;
        uint8_t type;
        Bytes payload;
    };

    xgl_config_t config = {};
#if XGL_FEATURE_ADVANCED_RUNTIME
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
#endif
    std::vector<xgl_route_item_t> routes;
    xgl_handle_t handle = nullptr;
    std::vector<Delivery> deliveries;
    std::vector<xgl_error_t> errors;

    static xgl_error_t accept(xgl_handle_t, uint16_t source, uint8_t type,
                              const uint8_t* data, size_t length, void* user) {
        auto& self = *static_cast<Node*>(user);
        self.deliveries.push_back({source, type, copy_bytes(data, length)});
        return XGL_OK;
    }

    static void error(xgl_handle_t, xgl_error_t error, const char*,
                      void* user) {
        static_cast<Node*>(user)->errors.push_back(error);
    }
};

class XglNetworkMultihopTest : public ::testing::Test {
  protected:
    LoopbackPhyPair ab;
    LoopbackPhyPair bc;
    ObservedPhy a_phy;
    ObservedPhy b_to_a;
    ObservedPhy b_to_c;
    ObservedPhy c_phy;
    AuthenticationLedger authentication;
    Node a;
    Node b;
    Node c;
    const xgl_work_budget_t budget = {256U, 1000U};
    const Bytes payload = {0xA1U, 0xB2U, 0xC3U, 0xD4U};

    void SetUp() override {
        a_phy.delegate = ab.get_phy_a();
        b_to_a.delegate = ab.get_phy_b();
        b_to_c.delegate = bc.get_phy_a();
        c_phy.delegate = bc.get_phy_b();
        a.routes = {{3U, &a_phy.ops, 256U, 1000U, 1U}};
        b.routes = {{1U, &b_to_a.ops, 256U, 1000U, 1U},
                    {3U, &b_to_c.ops, 256U, 1000U, 1U}};
        c.routes = {{1U, &c_phy.ops, 256U, 1000U, 1U}};
        uint16_t source = 1U;
        for (Node* node : {&a, &b, &c}) {
            xgl_config_get_default(&node->config);
            xgl_test_use_host_allocator(&node->config);
            node->config.source_id = source++;
            node->config.memory.rx_buffer_size = 256U;
            node->config.protocol.max_frame_size = 256U;
            node->config.protocol.window_size = 1U;
            node->config.protocol.ack_timeout_ms = 100U;
            node->config.features.enable_fragmentation = false;
            node->config.rx_accept_callback = Node::accept;
            node->config.error_callback = Node::error;
            node->config.callback_user_data = node;
        }
    }

    void TearDown() override {
        /* Borrowed route, PHY, provider and callback storage still exists. */
        for (Node* node : {&a, &b, &c}) {
            xgl_destroy(node->handle);
            node->handle = nullptr;
        }
    }

    void Start(bool authenticated = false, uint32_t connection = 0U,
               uint32_t epoch = 0U) {
        for (Node* node : {&a, &b, &c}) {
            node->config.route_table = node->routes.data();
            node->config.route_table_len = node->routes.size();
            if (authenticated && node != &b) {
                node->config.auth_required = true;
                node->config.auth_provider = &authentication.provider;
            }
            ASSERT_EQ(xgl_config_validate(&node->config), XGL_OK);
            node->handle = xgl_create(&node->config);
            ASSERT_NE(node->handle, nullptr);
            ASSERT_EQ(xgl_init(node->handle), XGL_OK);
        }
        if (authenticated) {
            const auto tx = test_session_config(3U, connection, epoch);
            const auto rx =
                test_session_config(1U, connection, epoch, 7U, true);
            ASSERT_EQ(xgl_install_security_session(a.handle, &tx), XGL_OK);
            ASSERT_EQ(xgl_install_security_session(c.handle, &rx), XGL_OK);
        }
    }

#if XGL_FEATURE_ADVANCED_RUNTIME
    void StartScoped(bool protect = true, bool install = true,
                     uint32_t connection = 0U, uint32_t epoch = 0U) {
        for (Node* node : {&a, &b, &c}) {
            node->config.route_table = node->routes.data();
            node->config.route_table_len = node->routes.size();
            if (node != &b) {
                node->config.auth_provider = &authentication.provider;
                node->options.protect_installed_scopes = protect;
            }
            ASSERT_EQ(xgl_create_with_options(&node->config, &node->options,
                                              &node->handle),
                      XGL_OK);
            ASSERT_EQ(xgl_init(node->handle), XGL_OK);
        }
        if (install) {
            const auto tx = test_session_config(3U, connection, epoch);
            const auto rx =
                test_session_config(1U, connection, epoch, 7U, true);
            ASSERT_EQ(xgl_install_security_session(a.handle, &tx), XGL_OK);
            ASSERT_EQ(xgl_install_security_session(c.handle, &rx), XGL_OK);
        }
    }

    void ExpectAuthenticated(const std::vector<Bytes>& frames) {
        ASSERT_FALSE(frames.empty());
        for (const Bytes& bytes : frames) {
            xgl_wire_frame_view_t view = {};
            ASSERT_EQ(xgl_wire_decode_frame(&view, bytes.data(), bytes.size(),
                                            nullptr),
                      XGL_OK);
            EXPECT_TRUE(view.authenticated);
            EXPECT_EQ(view.auth_tag_len, authentication.provider.tag_len);
        }
    }
#endif

    xgl_error_t Send(Node& source, uint16_t target, uint32_t now,
                     bool reliable = true, const Bytes* data = nullptr,
                     uint32_t connection = 0U, uint32_t epoch = 0U) {
        const Bytes& bytes = data != nullptr ? *data : payload;
        xgl_tx_data_t request = {};
        request.target_id = target;
        request.data_type = 9U;
        request.data = bytes.data();
        request.data_len = bytes.size();
        request.reliable = reliable;
        request.timeout_ms = 100U;
        request.connection_id = connection;
        request.session_epoch = epoch;
        return xgl_send_at(source.handle, &request, now);
    }

    void DeliverDataToC(uint32_t now, xgl_error_t expected) {
        /* Deliver the real sender's DATA without its preceding HELLO so this
         * receive is responsible for any peer admission at C. */
        ASSERT_FALSE(a_phy.frames.empty());
        const Bytes& frame = a_phy.frames.back();
        ASSERT_EQ(b_to_c.delegate.tx(frame.data(), frame.size(),
                                     b_to_c.delegate.user_data),
                  XGL_OK);
        xgl_error_t result = XGL_OK;
        for (uint32_t elapsed = 0U; elapsed < 32U && result == XGL_OK;
             ++elapsed) {
            result = xgl_step(c.handle, now + elapsed, &budget);
        }
        EXPECT_EQ(result, expected);
    }

    void DeliverAndAcknowledge(uint32_t now) {
        /* First reliable TX includes HELLO. Leave bounded time for partial
         * reads and the reverse ACK, including the 16-byte read chunk. */
        for (uint32_t elapsed = 0U; elapsed < 32U; ++elapsed) {
            for (Node* node : {&a, &b, &c}) {
                ASSERT_EQ(xgl_step(node->handle, now + elapsed, &budget),
                          XGL_OK);
            }
        }
    }

    void ExpectForwarded(const Bytes& original, const Bytes& forwarded,
                         uint16_t source, uint16_t target, uint8_t type) {
        xgl_wire_frame_view_t before = {};
        xgl_wire_frame_view_t after = {};
        ASSERT_EQ(xgl_wire_decode_frame(&before, original.data(),
                                        original.size(), nullptr),
                  XGL_OK);
        ASSERT_EQ(xgl_wire_decode_frame(&after, forwarded.data(),
                                        forwarded.size(), nullptr),
                  XGL_OK);
        EXPECT_EQ(after.header.source_id, source);
        EXPECT_EQ(after.header.target_id, target);
        EXPECT_EQ(after.header.packet_type, type);
        EXPECT_EQ(after.header.ttl, before.header.ttl - 1U);
        EXPECT_EQ(after.header.packet_number, before.header.packet_number);
        EXPECT_EQ(copy_bytes(after.extensions, after.extensions_len),
                  copy_bytes(before.extensions, before.extensions_len));
        EXPECT_EQ(copy_bytes(after.payload, after.payload_len),
                  copy_bytes(before.payload, before.payload_len));
        EXPECT_EQ(
            copy_bytes(after.payload + after.payload_len, after.auth_tag_len),
            copy_bytes(before.payload + before.payload_len,
                       before.auth_tag_len));
    }
};

TEST_F(XglNetworkMultihopTest, ReliableDataAndReverseAckReleaseSenderWindow) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(Send(a, 3U, 0U), XGL_OK);
    EXPECT_EQ(Send(a, 3U, 0U), XGL_ERR_WINDOW_FULL);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(1U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c.deliveries.front().source, 1U);
    EXPECT_EQ(c.deliveries.front().type, 9U);
    EXPECT_EQ(c.deliveries.front().payload, payload);
    EXPECT_TRUE(b.deliveries.empty());
    ASSERT_EQ(a_phy.frames.size(), 2U);
    ASSERT_EQ(b_to_c.frames.size(), 2U);
    ASSERT_EQ(c_phy.frames.size(), 1U);
    ASSERT_EQ(b_to_a.frames.size(), 1U);
    ExpectForwarded(a_phy.frames.back(), b_to_c.frames.back(), 1U, 3U,
                    XGL_PACKET_TYPE_DATA);
    ExpectForwarded(c_phy.frames.front(), b_to_a.frames.front(), 3U, 1U,
                    XGL_PACKET_TYPE_ACK);

    ASSERT_EQ(Send(a, 3U, 35U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(36U));
    ASSERT_EQ(c.deliveries.size(), 2U);
    ASSERT_EQ(xgl_step(a.handle, 1000U, &budget), XGL_OK);
    EXPECT_EQ(a_phy.frames.size(), 3U);
}

TEST_F(XglNetworkMultihopTest, MissingReverseRouteRejectsBeforeAdmission) {
    c.routes[0].target_id = 2U;
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(Send(a, 3U, 0U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverDataToC(1U, XGL_ERR_ROUTE_NOT_FOUND));
    EXPECT_TRUE(c.deliveries.empty());
    EXPECT_EQ(c.handle->layers.transport_ctx.peers, nullptr);
    EXPECT_EQ(c_phy.attempts, 0U);
}

TEST_F(XglNetworkMultihopTest,
       NarrowAuthenticatedReverseRouteRejectsBeforeAdmission) {
    authentication.provider.tag_len = 16U;
    c.routes[0].max_frame_size = 85U;
    ASSERT_NO_FATAL_FAILURE(Start(true, 7U, 9U));
    ASSERT_EQ(Send(a, 3U, 0U, true, nullptr, 7U, 9U), XGL_OK);
    const size_t signed_before = authentication.signed_inputs.size();
    ASSERT_NO_FATAL_FAILURE(DeliverDataToC(1U, XGL_ERR_BUFFER_TOO_SMALL));
    EXPECT_TRUE(c.deliveries.empty());
    EXPECT_EQ(c.handle->layers.transport_ctx.peers, nullptr);
    EXPECT_EQ(c_phy.attempts, 0U);
    EXPECT_EQ(authentication.signed_inputs.size(), signed_before);
    EXPECT_EQ(authentication.verified_by_target[3], 1U);
}

TEST_F(XglNetworkMultihopTest, AuthenticatedReverseRouteFitsAtExactFrameSize) {
    authentication.provider.tag_len = 16U;
    c.routes[0].max_frame_size = 86U;
    ASSERT_NO_FATAL_FAILURE(Start(true, 7U, 9U));
    ASSERT_EQ(Send(a, 3U, 0U, true, nullptr, 7U, 9U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverDataToC(1U, XGL_OK));
    ASSERT_EQ(c.deliveries.size(), 1U);
    ASSERT_EQ(c_phy.frames.size(), 1U);
    EXPECT_EQ(c_phy.frames[0].size(), 86U);
}

TEST_F(XglNetworkMultihopTest, BusyAcknowledgmentPreservesAcceptedData) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(Send(a, 3U, 0U), XGL_OK);
    c_phy.next_error = XGL_ERR_BUSY;
    ASSERT_NO_FATAL_FAILURE(DeliverDataToC(1U, XGL_ERR_BUSY));
    EXPECT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c_phy.attempts, 1U);
    EXPECT_TRUE(c_phy.frames.empty());
    ASSERT_NO_FATAL_FAILURE(DeliverDataToC(35U, XGL_OK));
    EXPECT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c_phy.attempts, 2U);
    EXPECT_EQ(c_phy.frames.size(), 1U);
}

TEST_F(XglNetworkMultihopTest,
       AuthenticatedEndpointsTrustEachOtherAcrossRelay) {
    ASSERT_NO_FATAL_FAILURE(Start(true));
    ASSERT_EQ(Send(a, 3U, 0U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(1U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c.deliveries.front().payload, payload);
    EXPECT_EQ(authentication.verified_by_target[3], 2U);
    EXPECT_EQ(authentication.verified_by_target[1], 1U);
    EXPECT_EQ(authentication.verified_by_target[2], 0U);
    EXPECT_TRUE(b.deliveries.empty());
    ASSERT_EQ(a_phy.frames.size(), 2U);
    ASSERT_EQ(c_phy.frames.size(), 1U);
    ASSERT_EQ(b_to_c.frames.size(), 2U);
    ASSERT_EQ(b_to_a.frames.size(), 1U);
    ExpectForwarded(a_phy.frames.back(), b_to_c.frames.back(), 1U, 3U,
                    XGL_PACKET_TYPE_DATA);
    ExpectForwarded(c_phy.frames.front(), b_to_a.frames.front(), 3U, 1U,
                    XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(Send(a, 3U, 35U), XGL_OK);
}

TEST_F(XglNetworkMultihopTest,
       UnknownExtensionsRemainAuthenticatedAcrossRelay) {
    ASSERT_NO_FATAL_FAILURE(Start(true));
    const uint8_t extension[] = {0x60U, 3U, 0x19U, 0x28U, 0x37U};
    xgl_packet_data_t data = {payload.size(), payload.data()};
    xgl_packet_t packet = {};
    packet.target_id = 3U;
    packet.data_type = 9U;
    packet.data = &data;
    packet.extensions = extension;
    packet.extensions_len = sizeof(extension);
    /* The public application API has no arbitrary TLV input. This uses A's
     * actual network/security/datalink stack and the regular B/C RX paths. */
    ASSERT_EQ(xgl_network_send(&a.handle->layers.network_ctx, &packet), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(1U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c.deliveries.front().payload, payload);
    EXPECT_EQ(authentication.verified_by_target[3], 1U);
    ASSERT_EQ(a_phy.frames.size(), 1U);
    ASSERT_EQ(b_to_c.frames.size(), 1U);
    ExpectForwarded(a_phy.frames.front(), b_to_c.frames.front(), 1U, 3U,
                    XGL_PACKET_TYPE_DATA);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, b_to_c.frames.front().data(),
                                    b_to_c.frames.front().size(), nullptr),
              XGL_OK);
    ASSERT_GE(view.extensions_len, sizeof(extension));
    EXPECT_TRUE(std::equal(std::begin(extension), std::end(extension),
                           view.extensions));
    EXPECT_TRUE(c_phy.frames.empty());
}

TEST_F(XglNetworkMultihopTest, RelayEgressMtuRejectsBeforePhySubmission) {
    b.routes[1].max_frame_size = 64U;
    ASSERT_NO_FATAL_FAILURE(Start());
    const Bytes larger_payload(80U, 0x42U);
    ASSERT_EQ(Send(a, 3U, 0U, false, &larger_payload), XGL_OK);
    xgl_error_t status = XGL_OK;
    for (uint32_t now = 1U; now < 32U && status == XGL_OK; ++now) {
        status = xgl_step(b.handle, now, &budget);
    }
    EXPECT_EQ(status, XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(b_to_c.attempts, 0U);
    ASSERT_EQ(xgl_step(c.handle, 32U, &budget), XGL_OK);
    EXPECT_TRUE(c.deliveries.empty());
    EXPECT_TRUE(c_phy.frames.empty());

    ASSERT_EQ(Send(a, 3U, 35U, false), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(36U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c.deliveries.front().payload, payload);
}

TEST_F(XglNetworkMultihopTest,
       BusyRelayNeedsEndpointRetryBeforeAcknowledgment) {
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(Send(a, 3U, 0U), XGL_OK);
    uint32_t now = 1U;
    while (b_to_c.frames.empty() && now < 32U) {
        ASSERT_EQ(xgl_step(b.handle, now, &budget), XGL_OK);
        ASSERT_EQ(xgl_step(c.handle, now++, &budget), XGL_OK);
    }
    ASSERT_EQ(b_to_c.frames.size(), 1U);
    b_to_c.next_error = XGL_ERR_BUSY;
    while (b_to_c.attempts < 2U && now < 64U) {
        const xgl_error_t status = xgl_step(b.handle, now, &budget);
        EXPECT_EQ(status, b_to_c.attempts == 2U ? XGL_ERR_BUSY : XGL_OK);
        ASSERT_EQ(xgl_step(c.handle, now++, &budget), XGL_OK);
    }
    EXPECT_EQ(b_to_c.attempts, 2U);
    EXPECT_EQ(b_to_c.frames.size(), 1U);
    EXPECT_TRUE(c.deliveries.empty());
    EXPECT_TRUE(c_phy.frames.empty());
    EXPECT_EQ(Send(a, 3U, now), XGL_ERR_WINDOW_FULL);

    ASSERT_EQ(xgl_step(a.handle, 100U, &budget), XGL_OK);
    ASSERT_EQ(a_phy.frames.size(), 3U);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(101U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c.deliveries.front().payload, payload);
    EXPECT_EQ(b_to_c.attempts, 3U);
    EXPECT_EQ(c_phy.frames.size(), 1U);
    EXPECT_EQ(Send(a, 3U, 135U), XGL_OK);
}

TEST_F(XglNetworkMultihopTest, BroadcastIsLocalDeliveryWithoutFlooding) {
    a.routes.push_back({XGL_BROADCAST_ID, &a_phy.ops, 256U, 1000U, 1U});
    ASSERT_NO_FATAL_FAILURE(Start());
    ASSERT_EQ(Send(a, XGL_BROADCAST_ID, 0U, false), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(1U));
    ASSERT_EQ(b.deliveries.size(), 1U);
    EXPECT_EQ(b.deliveries.front().source, 1U);
    EXPECT_EQ(b.deliveries.front().payload, payload);
    EXPECT_EQ(b_to_a.attempts, 0U);
    EXPECT_EQ(b_to_c.attempts, 0U);
    EXPECT_TRUE(c.deliveries.empty());
}

#if XGL_FEATURE_ADVANCED_RUNTIME
TEST_F(XglNetworkMultihopTest,
       ReverseFeedbackBudgetUsesExactAuthenticationScope) {
    authentication.provider.tag_len = 16U;
    c.routes[0].max_frame_size = 85U;
    ASSERT_NO_FATAL_FAILURE(StartScoped(true, true, 7U, 9U));

    ASSERT_EQ(Send(a, 3U, 0U, true, nullptr, 8U, 9U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverDataToC(1U, XGL_OK));
    ASSERT_EQ(c.deliveries.size(), 1U);
    ASSERT_EQ(c_phy.frames.size(), 1U);
    EXPECT_EQ(c_phy.frames[0].size(), 55U);

    ASSERT_EQ(Send(a, 3U, 35U, true, nullptr, 7U, 9U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverDataToC(36U, XGL_ERR_BUFFER_TOO_SMALL));
    EXPECT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c_phy.attempts, 1U);
}

TEST_F(XglNetworkMultihopTest,
       InstalledScopeProtectsHelloDataRetryAndReverseAcknowledgment) {
    ASSERT_NO_FATAL_FAILURE(StartScoped());
    ASSERT_FALSE(a.config.auth_required);
    ASSERT_FALSE(c.config.auth_required);
    ASSERT_EQ(Send(a, 3U, 0U), XGL_OK);
    ASSERT_EQ(xgl_step(a.handle, 100U, &budget), XGL_OK);
    ASSERT_EQ(a_phy.frames.size(), 3U);
    ASSERT_NO_FATAL_FAILURE(ExpectAuthenticated(a_phy.frames));
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(101U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    ASSERT_NO_FATAL_FAILURE(ExpectAuthenticated(c_phy.frames));
    EXPECT_GT(authentication.verified_by_target[3], 0U);
    EXPECT_GT(authentication.verified_by_target[1], 0U);
    EXPECT_EQ(authentication.verified_by_target[2], 0U);

    ASSERT_EQ(Send(c, 1U, 140U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(141U));
    ASSERT_EQ(a.deliveries.size(), 1U);
    EXPECT_EQ(a.deliveries.front().payload, payload);
    ASSERT_NO_FATAL_FAILURE(ExpectAuthenticated(a_phy.frames));
    ASSERT_NO_FATAL_FAILURE(ExpectAuthenticated(c_phy.frames));
}

TEST_F(XglNetworkMultihopTest,
       InstalledScopeProtectsInplaceLayoutWithoutAuthenticatingOtherScopes) {
    ASSERT_NO_FATAL_FAILURE(StartScoped(true, true, 11U, 9U));
    xgl_tx_zerocopy_request_t request = {};
    request.connection_id = 11U;
    request.session_epoch = 9U;
    request.data.target_id = 3U;
    request.data.data_len = payload.size();
    xgl_tx_frame_layout_t protected_layout = {};
    ASSERT_EQ(xgl_send_zerocopy_layout(a.handle, &request, &protected_layout),
              XGL_OK);
    EXPECT_TRUE(authentication.signed_inputs.empty());
    EXPECT_TRUE(a_phy.frames.empty());

    xgl_tx_frame_layout_t plain_layout = {};
    request.session_epoch = 10U;
    ASSERT_EQ(xgl_send_zerocopy_layout(a.handle, &request, &plain_layout),
              XGL_OK);
    EXPECT_EQ(protected_layout.payload_offset - plain_layout.payload_offset,
              XGL_SECURITY_EXT_SIZE);
    EXPECT_EQ(protected_layout.frame_size - plain_layout.frame_size,
              XGL_SECURITY_EXT_SIZE + authentication.provider.tag_len);

    request.session_epoch = 9U;
    Bytes storage(protected_layout.frame_size);
    std::copy(payload.begin(), payload.end(),
              storage.begin() + protected_layout.payload_offset);
    request.data.buffer = storage.data();
    request.data.buffer_size = storage.size();
    request.data.data_offset = protected_layout.payload_offset;
    ASSERT_EQ(xgl_send_zerocopy_scoped_at(a.handle, &request, 0U), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(ExpectAuthenticated(a_phy.frames));
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(1U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    EXPECT_EQ(c.deliveries.front().payload, payload);

    ASSERT_EQ(xgl_close_security_session(a.handle, 3U, 11U, 9U), XGL_OK);
    EXPECT_EQ(xgl_send_zerocopy_layout(a.handle, &request, &protected_layout),
              XGL_ERR_NOT_FOUND);
    EXPECT_EQ(protected_layout.frame_size, 0U);
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(a.handle, &request, 40U),
              XGL_ERR_NOT_FOUND);
    EXPECT_EQ(a_phy.frames.size(), 1U);
}

TEST_F(XglNetworkMultihopTest, InstalledScopeRejectsPlaintextAfterClose) {
    ASSERT_NO_FATAL_FAILURE(StartScoped());
    xgl_frame_params_t params = {};
    params.source_id = 1U;
    params.target_id = 3U;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload.data();
    params.payload_len = payload.size();
    params.ttl = 8U;
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    uint8_t bytes[128] = {};
    size_t length = 0U;
    ASSERT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &length),
              XGL_OK);
    EXPECT_EQ(xgl_network_receive(&c.handle->layers.network_ctx, c.handle,
                                  bytes, length),
              XGL_ERR_INVALID_FRAME);
    EXPECT_TRUE(c.deliveries.empty());
    ASSERT_EQ(xgl_close_security_session(c.handle, 1U, 0U, 0U), XGL_OK);
    EXPECT_EQ(xgl_network_receive(&c.handle->layers.network_ctx, c.handle,
                                  bytes, length),
              XGL_ERR_INVALID_FRAME);
    EXPECT_TRUE(c.deliveries.empty());
    EXPECT_EQ(Send(c, 1U, 0U, false), XGL_ERR_NOT_FOUND);
    EXPECT_TRUE(c_phy.frames.empty());

    /* An uninstalled connection remains outside this exact scope. */
    params.connection_id = 12U;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    ASSERT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &length),
              XGL_OK);
    EXPECT_EQ(xgl_network_receive(&c.handle->layers.network_ctx, c.handle,
                                  bytes, length),
              XGL_OK);
    EXPECT_EQ(c.deliveries.size(), 1U);
}

TEST_F(XglNetworkMultihopTest, DisabledScopeProtectionPreservesOptionalMode) {
    ASSERT_NO_FATAL_FAILURE(StartScoped(false));
    ASSERT_EQ(Send(a, 3U, 0U, false), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(DeliverAndAcknowledge(1U));
    ASSERT_EQ(c.deliveries.size(), 1U);
    EXPECT_TRUE(authentication.signed_inputs.empty());
    EXPECT_EQ(authentication.verified_by_target[3], 0U);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, a_phy.frames.front().data(),
                                    a_phy.frames.front().size(), nullptr),
              XGL_OK);
    EXPECT_FALSE(view.authenticated);
}

TEST_F(XglNetworkMultihopTest, InstalledScopeProtectsResetAndSelectiveAck) {
    ASSERT_NO_FATAL_FAILURE(StartScoped());
    ASSERT_EQ(transport_send_control(&a.handle->layers.transport_ctx, a.handle,
                                     3U, XGL_TRANSPORT_CONTROL_RESET, 0U, 0U),
              XGL_OK);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, a_phy.frames.front().data(),
                                    a_phy.frames.front().size(), nullptr),
              XGL_OK);
    EXPECT_EQ(view.header.packet_type, XGL_PACKET_TYPE_CONTROL);
    EXPECT_EQ(view.data_type, XGL_TRANSPORT_CONTROL_RESET);
    EXPECT_EQ(xgl_security_verify_frame(&c.handle->security, &view), XGL_OK);
    ASSERT_NO_FATAL_FAILURE(ExpectAuthenticated(a_phy.frames));

#if XGL_FEATURE_OUT_OF_ORDER
    xgl_transport_peer_state_t peer = {};
    xgl_transport_rx_buffered_packet_t buffered = {};
    buffered.packet.packet_number = 2U;
    peer.rx_buffered = &buffered;
    ASSERT_EQ(transport_send_sack(&c.handle->layers.transport_ctx, c.handle,
                                  &peer, 1U, 0U, 0U, 0U),
              XGL_OK);
    ASSERT_NO_FATAL_FAILURE(ExpectAuthenticated(c_phy.frames));
    ASSERT_EQ(xgl_wire_decode_frame(&view, c_phy.frames.front().data(),
                                    c_phy.frames.front().size(), nullptr),
              XGL_OK);
    EXPECT_EQ(view.header.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(xgl_security_verify_frame(&a.handle->security, &view), XGL_OK);
#endif
}
#endif

}  // namespace
