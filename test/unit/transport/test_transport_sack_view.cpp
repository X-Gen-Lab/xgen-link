/**
 * \file            test_transport_sack_view.cpp
 * \brief           Bounded SACK admission without partial peer mutations
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_host_allocator.h"
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>

#include <array>
#include <gtest/gtest.h>

namespace {

class XglTransportSackViewTest : public ::testing::Test {
  protected:
    xgl_transport_ctx_t context = {};
    xgl_layer_stats_t statistics = {};
    uint64_t retries = 0U;
    unsigned sends = 0U;
    unsigned sends_before_ack = 0U;
    xgl_packet_tx_endpoint_t lower = {};
    bool initialized = false;

    static xgl_error_t SendPacket(void* context, xgl_handle_t, xgl_packet_t*) {
        ++static_cast<XglTransportSackViewTest*>(context)->sends;
        return XGL_OK;
    }

    void SetUp() override {
        lower.ctx = this;
        lower.send = SendPacket;
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
        ASSERT_EQ(xgl_transport_init(&context, &config), XGL_OK);
        initialized = true;

        SendPackets(3U);
        ASSERT_NE(context.peers, nullptr);
        sends_before_ack = sends;
    }

    void SendPackets(unsigned count) {
        const uint8_t payload = 0x42;
        xgl_tx_data_t data = {};
        data.target_id = 2U;
        data.data = &payload;
        data.data_len = 1U;
        data.reliable = true;
        for (unsigned i = 0U; i < count; ++i) {
            ASSERT_EQ(xgl_transport_send(&context, nullptr, &data), XGL_OK);
        }
    }

    void TearDown() override {
        if (initialized) {
            xgl_transport_destroy(&context);
        }
    }

    xgl_error_t Receive(const uint8_t* bitmap, size_t length,
                        uint32_t base_packet = 0U) {
        std::array<uint8_t, 80> value = {};
        size_t value_size = 0U;
        xgl_error_t error = xgl_wire_encode_sack_ext_value(
            value.data(), value.size(), base_packet, bitmap, length,
            &value_size);
        if (error != XGL_OK) {
            return error;
        }
        return ReceiveExtension(XGL_WIRE_EXT_SACK, value.data(), value_size);
    }

    xgl_error_t ReceiveExtension(uint8_t type, const uint8_t* value,
                                 size_t value_size) {
        std::array<uint8_t, 80> extensions = {};
        size_t extensions_size = 0U;
        xgl_error_t error =
            xgl_wire_encode_ext(extensions.data(), extensions.size(), type,
                                value, value_size, &extensions_size);
        if (error != XGL_OK) {
            return error;
        }
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.packet_type = XGL_PACKET_TYPE_ACK;
        packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.reliable = XGL_RELIABILITY_ACK_ONLY;
        packet.extensions = extensions.data();
        packet.extensions_len = extensions_size;
        return xgl_transport_receive(&context, nullptr, &packet);
    }

    void ExpectUnchanged() {
        EXPECT_EQ(xgl_reliable_get_count(&context.peers->reliable_queue), 3U);
        EXPECT_EQ(context.peers->tx_window.send_base_packet_number, 0U);
        EXPECT_EQ(context.peers->tx_window.next_packet_number, 3U);
#if XGL_FEATURE_ADAPTIVE_RTO
        EXPECT_FALSE(xgl_rtt_is_initialized(&context.peers->rtt_est));
#endif
#if XGL_FEATURE_STATISTICS
        EXPECT_EQ(context.rtt_sample_count, 0U);
#endif
        EXPECT_FALSE(context.peers->failed);
        for (uint32_t packet_number = 0U; packet_number < 3U; ++packet_number) {
            const xgl_reliable_packet_t* packet =
                xgl_reliable_find_packet_number(&context.peers->reliable_queue,
                                                packet_number, 2U);
            ASSERT_NE(packet, nullptr);
            EXPECT_FALSE(packet->fast_retry_pending);
            EXPECT_FALSE(packet->retry_pending);
            EXPECT_EQ(packet->retry_count, 0U);
        }
        EXPECT_EQ(sends, sends_before_ack);
        EXPECT_EQ(retries, 0U);
    }
};

} /* namespace */

#if XGL_BUILD_WINDOW_CAPACITY >= 4
TEST_F(XglTransportSackViewTest, AcceptsTheExisting64ByteBitmapLimit) {
    std::array<uint8_t, 64> bitmap = {};
    bitmap[0] = 0x07;
    ASSERT_EQ(Receive(bitmap.data(), bitmap.size()), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&context.peers->reliable_queue), 0U);
    EXPECT_EQ(context.peers->tx_window.send_base_packet_number, 3U);
    EXPECT_EQ(sends, sends_before_ack);
    EXPECT_EQ(retries, 0U);
}

TEST_F(XglTransportSackViewTest, OversizedBitmapCannotPartiallyAcknowledge) {
    std::array<uint8_t, 65> bitmap = {};
    bitmap[0] = 0x07;
    EXPECT_EQ(Receive(bitmap.data(), bitmap.size()), XGL_ERR_BUFFER_TOO_SMALL);
    ExpectUnchanged();
}

TEST_F(XglTransportSackViewTest, EmptyBitmapCannotChangeTheOwningPeer) {
    EXPECT_EQ(Receive(nullptr, 0U), XGL_ERR_INVALID_FRAME);
    ExpectUnchanged();
}

TEST_F(XglTransportSackViewTest, FutureBitRejectsTheWholeBorrowedBitmap) {
    std::array<uint8_t, 64> bitmap = {};
    bitmap[0] = 0x07;
    bitmap.back() = 0x80;
    EXPECT_EQ(Receive(bitmap.data(), bitmap.size()), XGL_ERR_SEQUENCE_ERROR);
    ExpectUnchanged();
}

TEST_F(XglTransportSackViewTest, BaseDoesNotCumulativelyAcknowledge) {
    const uint8_t received = 1U;
    ASSERT_EQ(Receive(&received, 1U, 2U), XGL_OK);
    auto* peer = context.peers;
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 2U);
    EXPECT_EQ(peer->tx_window.send_base_packet_number, 0U);
    EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 2U, 2U),
              nullptr);

    const uint8_t missing = 0U;
    ASSERT_EQ(Receive(&missing, 1U, 1U), XGL_OK);
    const auto* before_base =
        xgl_reliable_find_packet_number(&peer->reliable_queue, 0U, 2U);
    const auto* at_base =
        xgl_reliable_find_packet_number(&peer->reliable_queue, 1U, 2U);
    ASSERT_NE(before_base, nullptr);
    ASSERT_NE(at_base, nullptr);
    EXPECT_FALSE(before_base->fast_retry_pending);
    EXPECT_TRUE(at_base->fast_retry_pending);
    EXPECT_EQ(sends, sends_before_ack);
}

TEST_F(XglTransportSackViewTest,
       CrossByteAcknowledgementsIgnoreTrailingZeroBytes) {
    const uint8_t acknowledged = 0x07;
    for (uint32_t base_packet : {0U, 3U}) {
        ASSERT_EQ(Receive(&acknowledged, 1U, base_packet), XGL_OK);
        SendPackets(3U);
    }
    ASSERT_EQ(context.peers->tx_window.send_base_packet_number, 6U);
    ASSERT_EQ(context.peers->tx_window.next_packet_number, 9U);
    const unsigned before_ack = sends;

    const std::array<uint8_t, 8> bitmap = {0xc0, 0x01};
    ASSERT_EQ(Receive(bitmap.data(), bitmap.size()), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&context.peers->reliable_queue), 0U);
    EXPECT_EQ(context.peers->tx_window.send_base_packet_number, 9U);
    EXPECT_EQ(sends, before_ack);
    EXPECT_EQ(retries, 0U);
}

TEST_F(XglTransportSackViewTest, CrossByteFutureBitCannotPartiallyAcknowledge) {
    const std::array<uint8_t, 4> bitmap = {0x07, 0x01};
    EXPECT_EQ(Receive(bitmap.data(), bitmap.size()), XGL_ERR_SEQUENCE_ERROR);
    ExpectUnchanged();
}

TEST_F(XglTransportSackViewTest, BaseNearPacketNumberLimitCannotWrapRange) {
    const uint8_t bitmap = 0x07;
    EXPECT_EQ(Receive(&bitmap, 1U, UINT32_MAX - 1U), XGL_ERR_SEQUENCE_ERROR);
    ExpectUnchanged();
    EXPECT_EQ(Receive(&bitmap, 1U, UINT32_MAX), XGL_ERR_SEQUENCE_ERROR);
    ExpectUnchanged();
}

TEST_F(XglTransportSackViewTest, ZeroLengthAckRangeCannotPartiallyAcknowledge) {
    const std::array<xgl_wire_ack_range_t, 2> ranges = {{{0U, 1U}, {0U, 0U}}};
    std::array<uint8_t, 32> value = {};
    size_t value_size = 0U;
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(value.data(), value.size(),
                                                  2U, 0U, ranges.data(),
                                                  ranges.size(), &value_size),
              XGL_OK);
    EXPECT_EQ(
        ReceiveExtension(XGL_WIRE_EXT_ACK_RANGE, value.data(), value_size),
        XGL_ERR_INVALID_FRAME);
    ExpectUnchanged();
}
#endif
