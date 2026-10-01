/**
 * \file            test_transport_sack_view.cpp
 * \brief           Bounded SACK admission without partial peer mutations
 * \author          X-Gen Lab
 */

#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>
#include <xgen/memory/libc_allocator.h>

#include <array>
#include <gtest/gtest.h>

namespace {

class XglTransportSackViewTest : public ::testing::Test {
  protected:
    xgl_transport_ctx_t context = {};
    xgl_layer_stats_t statistics = {};
    uint64_t retries = 0U;
    unsigned sends = 0U;
    xgl_packet_interface_t lower = {};
    bool initialized = false;

    static xgl_error_t SendPacket(void* context, xgl_handle_t, xgl_packet_t*) {
        ++static_cast<XglTransportSackViewTest*>(context)->sends;
        return XGL_OK;
    }

    void SetUp() override {
        lower.ctx = this;
        lower.send = SendPacket;
        xgl_transport_config_t config = {};
        config.allocator = xgm_allocator_libc();
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

        const uint8_t payload = 0x42;
        xgl_tx_data_t data = {};
        data.target_id = 2U;
        data.data = &payload;
        data.data_len = 1U;
        data.reliable = true;
        for (unsigned i = 0U; i < 3U; ++i) {
            ASSERT_EQ(xgl_transport_send(&context, nullptr, &data), XGL_OK);
        }
        ASSERT_NE(context.peers, nullptr);
    }

    void TearDown() override {
        if (initialized) {
            xgl_transport_destroy(&context);
        }
    }

    xgl_error_t Receive(const uint8_t* bitmap, size_t length) {
        std::array<uint8_t, 80> value = {}, extensions = {};
        size_t value_size = 0U, extensions_size = 0U;
        xgl_error_t error = xgl_wire_encode_sack_ext_value(
            value.data(), value.size(), 0U, bitmap, length, &value_size);
        if (error != XGL_OK) {
            return error;
        }
        error = xgl_wire_encode_ext(extensions.data(), extensions.size(),
                                    XGL_WIRE_EXT_SACK, value.data(), value_size,
                                    &extensions_size);
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
        EXPECT_FALSE(xgl_rtt_is_initialized(&context.peers->rtt_est));
        EXPECT_FALSE(context.peers->failed);
        EXPECT_EQ(sends, 3U);
        EXPECT_EQ(retries, 0U);
    }
};

}  // namespace

TEST_F(XglTransportSackViewTest, AcceptsTheExisting64ByteBitmapLimit) {
    std::array<uint8_t, 64> bitmap = {};
    bitmap[0] = 0x07;
    ASSERT_EQ(Receive(bitmap.data(), bitmap.size()), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&context.peers->reliable_queue), 0U);
    EXPECT_EQ(context.peers->tx_window.send_base_packet_number, 3U);
    EXPECT_EQ(sends, 3U);
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
