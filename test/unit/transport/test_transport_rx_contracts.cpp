/**
 * \file            test_transport_rx_contracts.cpp
 * \brief           Receive admission and acknowledgement failure contracts
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_host_allocator.h"
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_crc16.h>
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>
#include <wire/xgl_wire_bytes.h>
#include <xgl/xgl.h>

#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

class XglTransportRxContract : public ::testing::Test {
  protected:
    xgl_transport_ctx_t context = {};
    xgl_layer_stats_t statistics = {};
    uint64_t retries = 0U;
    xgl_packet_tx_endpoint_t lower = {};
    xgl_error_t feedback_error = XGL_OK;
    xgl_error_t application_error = XGL_OK;
    size_t feedback_attempts = 0U;
    std::vector<uint8_t> delivered;
    bool initialized = false;

    static xgl_error_t Send(void* opaque, xgl_handle_t, xgl_packet_t*) {
        auto& self = *static_cast<XglTransportRxContract*>(opaque);
        ++self.feedback_attempts;
        return self.feedback_error;
    }

    static xgl_error_t Accept(xgl_handle_t, uint16_t, uint8_t,
                              const uint8_t* data, size_t len, void* opaque) {
        auto& self = *static_cast<XglTransportRxContract*>(opaque);
        if (self.application_error == XGL_OK) {
            self.delivered.insert(self.delivered.end(), data, data + len);
        }
        return self.application_error;
    }

    void SetUp() override {
        lower = {this, Send, nullptr, nullptr};
        xgl_transport_config_t config = {};
        config.allocator = xgl_test_host_allocator();
        config.max_peers = 1U;
        config.max_tx_packets = 4U;
        config.max_rx_buffered_packets = 4U;
        config.local_id = 1U;
        config.max_retry_count = 3U;
        config.default_timeout_ms = 100U;
        config.window_size = 4U;
        config.max_frame_size = 128U;
        config.lower_layer = &lower;
        config.stats = &statistics;
        config.tx_retries = &retries;
        config.rx_accept_callback = Accept;
        config.callback_user_data = this;
        ASSERT_EQ(xgl_transport_init(&context, &config), XGL_OK);
        initialized = true;
    }

    void TearDown() override {
        if (initialized) {
            xgl_transport_destroy(&context);
        }
    }

    xgl_error_t Receive(uint32_t number, uint8_t value) {
        xgl_packet_data_t data = {1U, &value};
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.packet_number = number;
        packet.packet_type = XGL_PACKET_TYPE_DATA;
        packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
        packet.data = &data;
        return xgl_transport_receive(&context, nullptr, &packet);
    }
};

struct PublicReceiver {
    std::vector<uint8_t> input;
    size_t cursor = 0U;
    size_t delivered = 0U;
    size_t feedback_attempts = 0U;
    uint32_t now_ms = 0U;
    xgl_error_t feedback_error = XGL_OK;

    static xgl_error_t Read(uint8_t* output, size_t* len, void* opaque) {
        auto& self = *static_cast<PublicReceiver*>(opaque);
        *len = std::min(*len, self.input.size() - self.cursor);
        if (*len > 0U) {
            std::memcpy(output, self.input.data() + self.cursor, *len);
            self.cursor += *len;
        }
        return XGL_OK;
    }

    static xgl_error_t Write(const uint8_t*, size_t, void* opaque) {
        auto& self = *static_cast<PublicReceiver*>(opaque);
        ++self.feedback_attempts;
        return self.feedback_error;
    }

    static xgl_error_t Accept(xgl_handle_t, uint16_t, uint8_t, const uint8_t*,
                              size_t, void* opaque) {
        ++static_cast<PublicReceiver*>(opaque)->delivered;
        return XGL_OK;
    }

    xgl_phy_ops_t phy = {Write, Read, this};
    xgl_route_item_t route = {2U, &phy, 128U, 1000U, 0U};
    xgl_config_t config = {};
    std::unique_ptr<xgl_instance, decltype(&xgl_destroy)> instance{
        nullptr, &xgl_destroy};

    void Init(bool with_handler = true) {
        xgl_config_get_default(&config);
        config.memory.allocator = xgl_test_host_allocator();
        config.protocol.max_frame_size = 128U;
        config.memory.rx_buffer_size = 128U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.rx_accept_callback = with_handler ? Accept : nullptr;
        config.callback_user_data = this;
        ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
        instance.reset(xgl_create(&config));
        ASSERT_NE(instance, nullptr);
        ASSERT_EQ(xgl_init(instance.get()), XGL_OK);
    }

    xgl_error_t Feed(const std::vector<uint8_t>& frame) {
        input = frame;
        cursor = 0U;
        xgl_error_t first_error = XGL_OK;
        const xgl_work_budget_t budget = {128U, 1000U};
        for (unsigned attempt = 0U; cursor < input.size() && attempt < 100U;
             ++attempt) {
            xgl_error_t error = xgl_step(instance.get(), now_ms++, &budget);
            if (first_error == XGL_OK) {
                first_error = error;
            }
        }
        EXPECT_EQ(cursor, input.size());
        return first_error;
    }
};

std::vector<uint8_t> MakeFrame(uint8_t type, bool reliable = false,
                               uint16_t target = 1U,
                               const std::vector<uint8_t>& extensions = {},
                               size_t payload_size = 1U) {
    const std::vector<uint8_t> payload(payload_size, 0x42U);
    xgl_frame_params_t parameters = {};
    parameters.source_id = 2U;
    parameters.target_id = target;
    parameters.packet_type = type;
    parameters.reliable = reliable;
    parameters.extensions = extensions.empty() ? nullptr : extensions.data();
    parameters.extensions_len = extensions.size();
    parameters.payload = payload.data();
    parameters.payload_len = payload.size();
    parameters.ttl = 4U;
    xgl_frame_t frame = {};
    EXPECT_EQ(xgl_frame_build(&frame, &parameters), XGL_OK);
    std::vector<uint8_t> encoded(XGL_FRAME_HEADER_SIZE + extensions.size() +
                                 payload.size() + XGL_CRC16_SIZE);
    size_t size = 0U;
    EXPECT_EQ(
        xgl_frame_serialize(encoded.data(), encoded.size(), &frame, &size),
        XGL_OK);
    encoded.resize(size);
    return encoded;
}

void RefreshFrameCrc(std::vector<uint8_t>& encoded) {
    const size_t header_crc = XGL_FRAME_HEADER_SIZE - XGL_CRC16_SIZE;
    xgl_serialize_u16_le(encoded.data() + header_crc, 0U);
    xgl_serialize_u16_le(
        encoded.data() + header_crc,
        xgl_crc16_modbus(encoded.data(), XGL_FRAME_HEADER_SIZE));
    const size_t frame_crc = encoded.size() - XGL_CRC16_SIZE;
    xgl_serialize_u16_le(encoded.data() + frame_crc,
                         xgl_crc16_modbus(encoded.data(), frame_crc));
}

} /* namespace */

TEST(XglTransportRxWireContract, AcceptsVersionOneAndRejectsVersionThree) {
    PublicReceiver receiver;
    receiver.Init();
    const auto current = MakeFrame(XGL_PACKET_TYPE_DATA, true);
    ASSERT_GE(current.size(), XGL_FRAME_HEADER_SIZE);
    EXPECT_EQ(current[2], 1U);

    auto previous = current;
    previous[2] = 3U;
    RefreshFrameCrc(previous);
    (void)receiver.Feed(previous);
    EXPECT_EQ(receiver.delivered, 0U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);

    EXPECT_EQ(receiver.Feed(current), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 1U);
}

TEST(XglTransportRxWireContract,
     UnsupportedFlagsTrafficOrSecurityLayoutCannotDeliver) {
    PublicReceiver receiver;
    receiver.Init();
    const auto current =
        MakeFrame(XGL_PACKET_TYPE_DATA, true, 1U, {0x7EU, 1U, 0xAAU});
    ASSERT_GT(current.size(), XGL_FRAME_HEADER_SIZE);

    auto reserved_flag = current;
    reserved_flag[5] |= 0x10U;
    auto reserved_traffic_bit = current;
    reserved_traffic_bit[7] |= 0x20U;
    auto malformed_security = current;
    malformed_security[5] |= 0x04U;
    malformed_security[XGL_FRAME_HEADER_SIZE] = 4U;
    for (auto* frame :
         {&reserved_flag, &reserved_traffic_bit, &malformed_security}) {
        RefreshFrameCrc(*frame);
        (void)receiver.Feed(*frame);
        EXPECT_EQ(receiver.delivered, 0U);
        EXPECT_EQ(receiver.feedback_attempts, 0U);
    }

    EXPECT_EQ(receiver.Feed(current), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 1U);
}

TEST(XglTransportRxWireContract, OversizedFrameDoesNotConsumeReceiveSequence) {
    PublicReceiver receiver;
    receiver.Init();
    constexpr size_t payload_budget =
        128U - XGL_FRAME_HEADER_SIZE - XGL_CRC16_SIZE;
    const auto oversized =
        MakeFrame(XGL_PACKET_TYPE_DATA, true, 1U, {}, payload_budget + 1U);
    ASSERT_EQ(oversized.size(), 129U);
    (void)receiver.Feed(oversized);
    EXPECT_EQ(receiver.delivered, 0U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);

    const auto at_limit =
        MakeFrame(XGL_PACKET_TYPE_DATA, true, 1U, {}, payload_budget);
    ASSERT_EQ(at_limit.size(), 128U);
    EXPECT_EQ(receiver.Feed(at_limit), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 1U);
}

TEST(XglTransportRxWireContract, ReservedPacketTypesNeverReachApplication) {
    PublicReceiver receiver;
    receiver.Init();
    for (uint8_t type : {uint8_t{4U}, uint8_t{5U}, uint8_t{6U}, uint8_t{7U}}) {
        SCOPED_TRACE(static_cast<unsigned>(type));
        auto frame = MakeFrame(XGL_PACKET_TYPE_DATA);
        ASSERT_GT(frame.size(), XGL_FRAME_HEADER_SIZE);
        frame[4] = type;
        RefreshFrameCrc(frame);
        (void)receiver.Feed(frame);
    }
    EXPECT_EQ(receiver.delivered, 0U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);
    EXPECT_EQ(receiver.Feed(MakeFrame(XGL_PACKET_TYPE_DATA, true)), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 1U);
}

TEST(XglTransportRxWireContract, ReliableBroadcastPreservesUnicastHistory) {
    PublicReceiver receiver;
    receiver.Init();
    auto broadcast = MakeFrame(XGL_PACKET_TYPE_DATA, true, UINT16_MAX);
    ASSERT_FALSE(broadcast.empty());
    EXPECT_EQ(receiver.Feed(broadcast), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(receiver.delivered, 0U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);

    auto unicast = MakeFrame(XGL_PACKET_TYPE_DATA, true);
    ASSERT_FALSE(unicast.empty());
    EXPECT_EQ(receiver.Feed(unicast), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 1U);
}

TEST(XglTransportRxWireContract, UnreliableBroadcastStillDelivers) {
    PublicReceiver receiver;
    receiver.Init();
    auto frame = MakeFrame(XGL_PACKET_TYPE_DATA, false, UINT16_MAX);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(receiver.Feed(frame), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);
}

TEST(XglTransportRxWireContract, StepReportsFeedbackFailureWithoutRedelivery) {
    PublicReceiver receiver;
    receiver.Init();
    auto frame = MakeFrame(XGL_PACKET_TYPE_DATA, true);
    ASSERT_FALSE(frame.empty());
    receiver.feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(receiver.Feed(frame), XGL_ERR_TX_FAILED);
    EXPECT_EQ(receiver.delivered, 1U);
    receiver.feedback_error = XGL_OK;
    EXPECT_EQ(receiver.Feed(frame), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 2U);
}

TEST(XglTransportRxWireContract, MissingHandlerCannotAcknowledgeData) {
    PublicReceiver receiver;
    receiver.Init(false);
    const auto frame = MakeFrame(XGL_PACKET_TYPE_DATA, true);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(receiver.Feed(frame), XGL_ERR_UNSUPPORTED);
    EXPECT_EQ(receiver.feedback_attempts, 0U);
#if XGL_FEATURE_STATISTICS
    xgl_statistics_t statistics = {};
    ASSERT_EQ(xgl_stats_get(receiver.instance.get(), &statistics), XGL_OK);
    EXPECT_EQ(statistics.transport.rx_packets, 0U);
    EXPECT_EQ(statistics.transport.rx_bytes, 0U);
    EXPECT_EQ(statistics.transport.rx_dropped, 1U);
#endif
}

#if XGL_BUILD_WINDOW_CAPACITY >= 4
TEST_F(XglTransportRxContract, ReservedNumberCannotCreateReceiveHistory) {
    EXPECT_EQ(Receive(UINT32_MAX, 0x41U), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(context.peers, nullptr);
    EXPECT_EQ(feedback_attempts, 0U);
    EXPECT_TRUE(delivered.empty());
}

TEST_F(XglTransportRxContract, ExhaustedNumberCannotWrapOrRedeliverOldData) {
    ASSERT_EQ(Receive(0U, 0x41U), XGL_OK);
    ASSERT_NE(context.peers, nullptr);
    /* Model the reachable boundary after earlier numbers were accepted. */
    context.peers->rx_next_packet_number = UINT32_MAX - 1U;
    ASSERT_EQ(Receive(UINT32_MAX - 1U, 0x42U), XGL_OK);
    const auto attempts_before = feedback_attempts;
    EXPECT_EQ(Receive(UINT32_MAX, 0x43U), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(context.peers->rx_next_packet_number, UINT32_MAX);
    EXPECT_EQ(feedback_attempts, attempts_before);
    EXPECT_EQ(Receive(0U, 0x41U), XGL_OK);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U, 0x42U}));
}

TEST_F(XglTransportRxContract, FeedbackFailureKeepsAcceptedHistory) {
    feedback_error = XGL_ERR_BUSY;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_ERR_BUSY);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_next_packet_number, 1U);
    EXPECT_FALSE(context.peers->failed);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U}));
    EXPECT_EQ(Receive(0U, 0x41U), XGL_ERR_BUSY);
    EXPECT_EQ(delivered.size(), 1U);
    feedback_error = XGL_OK;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_OK);
    EXPECT_EQ(delivered.size(), 1U);
}
#endif

#if XGL_FEATURE_OUT_OF_ORDER
TEST_F(XglTransportRxContract, SackFailureRetainsOwnedPacket) {
    feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(Receive(1U, 0x42U), XGL_ERR_TX_FAILED);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_buffered_count, 1U);
    EXPECT_TRUE(context.peers->rx_has_packet_number_state);
    feedback_error = XGL_OK;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_OK);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U, 0x42U}));
    EXPECT_EQ(context.peers->rx_buffered_count, 0U);
}

TEST_F(XglTransportRxContract, FeedbackFailureStillDrainsContiguousOwnedData) {
    ASSERT_EQ(Receive(1U, 0x42U), XGL_OK);
    ASSERT_EQ(Receive(2U, 0x43U), XGL_OK);
    feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_ERR_TX_FAILED);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_next_packet_number, 3U);
    EXPECT_EQ(context.peers->rx_buffered_count, 0U);
    EXPECT_FALSE(context.peers->failed);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U, 0x42U, 0x43U}));
}

TEST_F(XglTransportRxContract, AdmissionErrorPrecedesFeedbackError) {
    context.max_rx_buffered_packets = 1U;
    ASSERT_EQ(Receive(1U, 0x42U), XGL_OK);
    feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(Receive(2U, 0x43U), XGL_ERR_WINDOW_FULL);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_buffered_count, 1U);
}
#endif

#if XGL_FEATURE_OUT_OF_ORDER
TEST_F(XglTransportRxContract, MissingHandlerCannotCacheFutureData) {
    context.rx_accept_callback = nullptr;
    auto* peer = transport_get_or_create_peer_scope(&context, 2U, 0U, 0U);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(Receive(1U, 0x41U), XGL_ERR_UNSUPPORTED);
    EXPECT_EQ(context.peers, peer);
    EXPECT_FALSE(peer->rx_has_packet_number_state);
    EXPECT_EQ(peer->rx_buffered_count, 0U);
    EXPECT_EQ(feedback_attempts, 0U);
}
#endif
