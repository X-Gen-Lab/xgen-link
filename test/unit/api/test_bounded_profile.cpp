/**
 * \file            test_bounded_profile.cpp
 * \brief           Capability-selected protocol contracts for bounded profiles
 * \author          X-Gen Lab
 */

#include <datalink/xgl_parser.h>
#include <wire/xgl_frame.h>
#include <xgl/xgl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

class XglBoundedProfileTest : public ::testing::Test {
  protected:
    struct Endpoint {
        Endpoint* remote = nullptr;
        std::vector<uint8_t> incoming;
        std::vector<uint8_t> delivered;
        unsigned deliveries = 0U;
        bool drop_next_transmission = false;
        bool reject_delivery = false;
    } a, b;

    std::array<xgl_max_align_t, 4096> storage_a{}, storage_b{};
    xgl_phy_ops_t phy_a{Transmit, Receive, &a};
    xgl_phy_ops_t phy_b{Transmit, Receive, &b};
    xgl_route_item_t route_a{2U, &phy_a, 128U, 1000U, 0U};
    xgl_route_item_t route_b{1U, &phy_b, 128U, 1000U, 0U};
    xgl_config_t config_a{}, config_b{};
    xgl_handle_t handle_a = nullptr;
    xgl_handle_t handle_b = nullptr;

    static xgl_error_t Transmit(const uint8_t* data, size_t len,
                                void* context) {
        auto* endpoint = static_cast<Endpoint*>(context);
        if (endpoint->drop_next_transmission) {
            endpoint->drop_next_transmission = false;
            return XGL_OK;
        }
        endpoint->remote->incoming.insert(endpoint->remote->incoming.end(),
                                          data, data + len);
        return XGL_OK;
    }

    static xgl_error_t Receive(uint8_t* data, size_t* len, void* context) {
        auto* endpoint = static_cast<Endpoint*>(context);
        *len = std::min(*len, endpoint->incoming.size());
        if (*len != 0U) {
            std::memcpy(data, endpoint->incoming.data(), *len);
            endpoint->incoming.erase(endpoint->incoming.begin(),
                                     endpoint->incoming.begin() + *len);
        }
        return XGL_OK;
    }

    static void Deliver(xgl_handle_t, uint16_t, uint8_t, const uint8_t* data,
                        size_t len, void* context) {
        auto* endpoint = static_cast<Endpoint*>(context);
        endpoint->delivered.assign(data, data + len);
        ++endpoint->deliveries;
    }

    static xgl_error_t Accept(xgl_handle_t handle, uint16_t source,
                              uint8_t type, const uint8_t* data, size_t len,
                              void* context) {
        if (static_cast<Endpoint*>(context)->reject_delivery) {
            return XGL_ERR_BUSY;
        }
        Deliver(handle, source, type, data, len, context);
        return XGL_OK;
    }

    void SetUp() override {
        a.remote = &b;
        b.remote = &a;
        xgl_config_get_preset_boot(&config_a);
        config_a.route_table = &route_a;
        config_a.route_table_len = 1U;
        config_a.rx_callback = Deliver;
        config_a.callback_user_data = &a;
        config_b = config_a;
        config_b.source_id = 2U;
        config_b.route_table = &route_b;
        config_b.callback_user_data = &b;
    }

    void Initialize() {
        ASSERT_EQ(xgl_init_static(&config_a, storage_a.data(),
                                  sizeof(storage_a), &handle_a),
                  XGL_OK);
        ASSERT_EQ(xgl_init_static(&config_b, storage_b.data(),
                                  sizeof(storage_b), &handle_b),
                  XGL_OK);
    }

    void TearDown() override {
        xgl_destroy(handle_a);
        xgl_destroy(handle_b);
    }

    xgl_error_t Send(const uint8_t* data, size_t len, uint32_t now_ms = 0U) {
        xgl_tx_data_t request{};
        request.target_id = 2U;
        request.data = data;
        request.data_len = len;
        request.reliable = true;
        return xgl_send_at(handle_a, &request, now_ms);
    }

    void Drive(unsigned steps = 16U, uint32_t now_ms = 0U) {
        const xgl_work_budget_t budget{128U, 100U};
        for (unsigned i = 0U; i < steps; ++i, ++now_ms) {
            ASSERT_EQ(xgl_step(handle_b, now_ms, &budget), XGL_OK);
            ASSERT_EQ(xgl_step(handle_a, now_ms, &budget), XGL_OK);
        }
    }
};

TEST_F(XglBoundedProfileTest, ReliableDeliveryUsesExplicitStorage) {
    Initialize();
    ASSERT_NE(handle_a, nullptr);
    const uint8_t payload[] = {1U, 2U, 3U, 4U};
    ASSERT_EQ(Send(payload, sizeof(payload)), XGL_OK);
    Drive();
    EXPECT_EQ(b.deliveries, 1U);
    EXPECT_EQ(b.delivered,
              std::vector<uint8_t>(payload, payload + sizeof(payload)));
}

TEST_F(XglBoundedProfileTest, LostAckRetransmitsWithoutRedelivery) {
    Initialize();
    const uint8_t first = 3U;
    const uint8_t second = 5U;
    b.drop_next_transmission = true;

    ASSERT_EQ(Send(&first, 1U), XGL_OK);
    EXPECT_EQ(Send(&second, 1U), XGL_ERR_WINDOW_FULL);
    Drive();
    EXPECT_FALSE(b.drop_next_transmission);
    EXPECT_EQ(b.deliveries, 1U);
    EXPECT_EQ(Send(&second, 1U, 16U), XGL_ERR_WINDOW_FULL);

    const uint32_t retry_at = config_a.protocol.ack_timeout_ms;
    Drive(16U, retry_at);
    EXPECT_EQ(b.deliveries, 1U);
    EXPECT_EQ(b.delivered, std::vector<uint8_t>{first});

    ASSERT_EQ(Send(&second, 1U, retry_at + 16U), XGL_OK);
    Drive(16U, retry_at + 16U);
    EXPECT_EQ(b.deliveries, 2U);
    EXPECT_EQ(b.delivered, std::vector<uint8_t>{second});
}

TEST_F(XglBoundedProfileTest, BusyReceiverRetainsSenderWindowUntilRetry) {
    config_b.rx_accept_callback = Accept;
    b.reject_delivery = true;
    Initialize();
    const uint8_t payload = 7U;
    ASSERT_EQ(Send(&payload, 1U), XGL_OK);

    const xgl_work_budget_t budget{128U, 100U};
    bool rejected = false;
    for (uint32_t now_ms = 0U; now_ms < 16U; ++now_ms) {
        const xgl_error_t result = xgl_step(handle_b, now_ms, &budget);
        ASSERT_TRUE(result == XGL_OK || result == XGL_ERR_BUSY);
        rejected |= result == XGL_ERR_BUSY;
        ASSERT_EQ(xgl_step(handle_a, now_ms, &budget), XGL_OK);
    }
    EXPECT_TRUE(rejected);
    EXPECT_EQ(b.deliveries, 0U);
    EXPECT_EQ(Send(&payload, 1U, 16U), XGL_ERR_WINDOW_FULL);

    b.reject_delivery = false;
    const uint32_t retry_at = config_a.protocol.ack_timeout_ms;
    Drive(16U, retry_at);
    EXPECT_EQ(b.deliveries, 1U);
    EXPECT_EQ(b.delivered, std::vector<uint8_t>{payload});
    EXPECT_EQ(Send(&payload, 1U, retry_at + 16U), XGL_OK);
}

TEST_F(XglBoundedProfileTest, RejectsUnsupportedAuthentication) {
    config_a.auth_required = true;
    EXPECT_EQ(xgl_config_validate(&config_a), XGL_ERR_UNSUPPORTED);
}

TEST_F(XglBoundedProfileTest, CapacityShortagePreservesCallerStorage) {
    xgl_memory_requirements_t required{};
    ASSERT_EQ(xgl_memory_requirements(&config_a, &required), XGL_OK);
    ASSERT_LT(required.size, sizeof(storage_a));
    std::memset(storage_a.data(), 0xA7, sizeof(storage_a));
    EXPECT_EQ(xgl_init_static(&config_a, storage_a.data(), required.size - 1U,
                              &handle_a),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(handle_a, nullptr);
    const auto* bytes = reinterpret_cast<const uint8_t*>(storage_a.data());
    EXPECT_EQ(bytes[0], 0xA7U);
    EXPECT_EQ(bytes[required.size - 1U], 0xA7U);
}

TEST_F(XglBoundedProfileTest, RejectsOversizedSingleFrameWithoutTransmit) {
    Initialize();
    std::array<uint8_t, 128> payload{};
    EXPECT_EQ(Send(payload.data(), payload.size()), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_TRUE(b.incoming.empty());
}

TEST_F(XglBoundedProfileTest, ParserRequiresCompleteValidatedFrame) {
    uint8_t encoded[128]{};
    const uint8_t payload[] = {3U, 5U, 7U};
    xgl_frame_params_t parameters{};
    parameters.source_id = 1U;
    parameters.target_id = 2U;
    parameters.packet_type = XGL_PACKET_TYPE_DATA;
    parameters.payload = payload;
    parameters.payload_len = sizeof(payload);
    parameters.ttl = 8U;
    xgl_frame_t frame{};
    ASSERT_EQ(xgl_frame_build(&frame, &parameters), XGL_OK);
    size_t written = 0U;
    ASSERT_EQ(xgl_frame_serialize(encoded, sizeof(encoded), &frame, &written),
              XGL_OK);
    uint8_t cache[128]{};
    xgl_parser_t parser{};
    ASSERT_EQ(xgl_parser_init(&parser, cache, sizeof(cache)), XGL_OK);
    for (size_t i = 0U; i + 1U < written; ++i) {
        ASSERT_EQ(xgl_parser_feed_byte(&parser, encoded[i], 0U),
                  XGL_PARSE_RESULT_INCOMPLETE);
    }
    EXPECT_EQ(xgl_parser_feed_byte(&parser, encoded[written - 1U], 0U),
              XGL_PARSE_RESULT_COMPLETE);
    xgl_parser_reset(&parser);
    encoded[written - 1U] ^= 1U;
    for (size_t i = 0U; i + 1U < written; ++i) {
        ASSERT_EQ(xgl_parser_feed_byte(&parser, encoded[i], 1U),
                  XGL_PARSE_RESULT_INCOMPLETE);
    }
    EXPECT_EQ(xgl_parser_feed_byte(&parser, encoded[written - 1U], 1U),
              XGL_PARSE_RESULT_ERROR);
}

#if XGL_FEATURE_FRAGMENTATION
TEST_F(XglBoundedProfileTest, FragmentedMessageCompletesBeyondWindow) {
    config_a.features.enable_fragmentation = true;
    config_a.features.max_message_size = 512U;
    config_a.features.max_reassembly_slots = 1U;
    config_a.features.max_reassembly_bytes = 512U;
    config_a.features.max_tx_message_bytes = 512U;
    config_b.features = config_a.features;
    Initialize();
    std::array<uint8_t, 384> payload{};
    for (size_t i = 0U; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }
    ASSERT_EQ(Send(payload.data(), payload.size()), XGL_OK);
    Drive(32U);
    EXPECT_EQ(b.deliveries, 1U);
    EXPECT_EQ(b.delivered,
              std::vector<uint8_t>(payload.begin(), payload.end()));
}
#endif
