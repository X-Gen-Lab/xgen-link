/**
 * \file            test_coverage_delivery.cpp
 * \brief           Receive admission, PHY failures and parser recovery
 * contracts
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_wire.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

class XglCoverageDelivery : public ::testing::Test {
  protected:
    xgl_datalink_ctx_t context = {};
    xgl_parser_t parser = {};
    xgl_layer_stats_t statistics = {};
    xgl_phy_ops_t phy = {};
    uint8_t cache[128] = {};
    uint8_t frame[128] = {};
    size_t frame_size = 0;
    size_t reads = 0;
    size_t requested = 0;
    bool exceed_capacity = false;
    xgl_error_t read_result = XGL_OK;
    std::vector<uint8_t> input;
    std::vector<xgl_error_t> errors;

    void SetUp() override {
        ASSERT_EQ(xgl_parser_init(&parser, cache, sizeof(cache)), XGL_OK);
        context.stats = &statistics;
        context.callback_user_data = this;
        context.error_callback = [](xgl_handle_t, xgl_error_t error,
                                    const char*, void* user) {
            static_cast<XglCoverageDelivery*>(user)->errors.push_back(error);
        };
        phy.user_data = this;
        phy.rx = [](uint8_t* bytes, size_t* length, void* user) {
            auto& self = *static_cast<XglCoverageDelivery*>(user);
            ++self.reads;
            self.requested = *length;
            if (self.read_result != XGL_OK) {
                return self.read_result;
            }
            if (self.exceed_capacity) {
                ++*length;
                return XGL_OK;
            }
            *length = std::min(*length, self.input.size());
            if (*length != 0U) {
                std::memcpy(bytes, self.input.data(), *length);
                self.input.erase(self.input.begin(),
                                 self.input.begin() + *length);
            }
            return XGL_OK;
        };
        xgl_frame_params_t params = {};
        params.source_id = 2;
        params.target_id = 1;
        params.packet_type = XGL_PACKET_TYPE_DATA;
        xgl_frame_t value = {};
        ASSERT_EQ(xgl_frame_build(&value, &params), XGL_OK);
        ASSERT_EQ(
            xgl_frame_serialize(frame, sizeof(frame), &value, &frame_size),
            XGL_OK);
    }
};

TEST_F(XglCoverageDelivery, InvalidPollArgumentsNeverCallTheDriver) {
    EXPECT_EQ(xgl_datalink_poll_parser(nullptr, &parser, &phy, 0, 10,
                                       XGL_DATALINK_RX_CHUNK_SIZE),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_poll_parser(nullptr, &parser, &phy, 0, 10, 8),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_poll_parser(&context, nullptr, &phy, 0, 10, 8),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, nullptr, 0, 10, 8),
              XGL_ERR_NULL_POINTER);
    phy.rx = nullptr;
    EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 0, 10,
                                       XGL_DATALINK_RX_CHUNK_SIZE),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(reads, 0U);
}

TEST_F(XglCoverageDelivery, PollHonorsCapacityAndPropagatesDriverFailures) {
    EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 0, 10, 0),
              XGL_OK);
    EXPECT_EQ(reads, 0U);
    read_result = XGL_ERR_BUSY;
    EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 0, 10, 3),
              XGL_ERR_BUSY);
    EXPECT_EQ(requested, 3U);
    read_result = XGL_OK;
    exceed_capacity = true;
    EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 0, 10,
                                       XGL_DATALINK_RX_CHUNK_SIZE),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(requested, static_cast<size_t>(XGL_DATALINK_RX_CHUNK_SIZE));
    EXPECT_EQ(parser.cache_len, 0U);
}

TEST_F(XglCoverageDelivery, TimedOutPartialFramesResetWithOptionalObservers) {
    for (bool observers : {true, false}) {
        context.stats = observers ? &statistics : nullptr;
        context.error_callback = observers ? context.error_callback : nullptr;
        ASSERT_EQ(xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_0, 1),
                  XGL_PARSE_RESULT_INCOMPLETE);
        ASSERT_EQ(xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_1, 1),
                  XGL_PARSE_RESULT_INCOMPLETE);
        EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 30, 10,
                                           XGL_DATALINK_RX_CHUNK_SIZE),
                  XGL_OK);
        EXPECT_EQ(parser.cache_len, 0U);
        EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    }
    EXPECT_EQ(statistics.rx_errors, 1U);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_TIMEOUT);
}

TEST_F(XglCoverageDelivery,
       CorruptFramesCountTheRightErrorWithOptionalObservers) {
    uint64_t header_errors = 0, frame_errors = 0;
    context.rx_header_crc_errors = &header_errors;
    context.rx_crc16_errors = &frame_errors;
    EXPECT_EQ(xgl_datalink_process_frame(nullptr, frame, frame_size),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_process_frame(&context, nullptr, frame_size),
              XGL_ERR_NULL_POINTER);
    frame[frame_size - 1] ^= 1U;
    EXPECT_EQ(xgl_datalink_process_frame(&context, frame, frame_size),
              XGL_ERR_CRC_FAILED);
    EXPECT_EQ(frame_errors, 1U);
    EXPECT_EQ(header_errors, 0U);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_CRC_FAILED);
    context.stats = nullptr;
    context.error_callback = nullptr;
    context.rx_crc16_errors = nullptr;
    EXPECT_EQ(xgl_datalink_process_frame(&context, frame, frame_size),
              XGL_ERR_CRC_FAILED);
    frame[frame_size - 1] ^= 1U;
    frame[23] ^= 1U;
    EXPECT_NE(xgl_datalink_process_frame(&context, frame, frame_size), XGL_OK);
    EXPECT_EQ(header_errors, 1U);
    context.rx_header_crc_errors = nullptr;
    EXPECT_NE(xgl_datalink_process_frame(&context, frame, frame_size), XGL_OK);
    EXPECT_EQ(statistics.rx_errors, 1U);
}

TEST_F(XglCoverageDelivery, OversizedFrameReportsAnErrorBeforeReadingItsBody) {
    std::vector<uint8_t> oversized(XGL_DATALINK_MAX_FRAME_SIZE + 1U, 0);
    EXPECT_EQ(xgl_datalink_process_frame(&context, oversized.data(),
                                         oversized.size()),
              XGL_ERR_INVALID_FRAME);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_INVALID_FRAME);
    context.error_callback = nullptr;
    EXPECT_EQ(xgl_datalink_process_frame(&context, oversized.data(),
                                         oversized.size()),
              XGL_ERR_INVALID_FRAME);
}

TEST_F(XglCoverageDelivery, ParserViewRetainsTheDatalinkFrameLimit) {
    std::vector<uint8_t> payload(XGL_DATALINK_MAX_FRAME_SIZE, 0x37);
    xgl_frame_params_t params = {};
    params.source_id = 2U;
    params.target_id = 1U;
    params.payload = payload.data();
    params.payload_len = payload.size();
    xgl_frame_t value = {};
    ASSERT_EQ(xgl_frame_build(&value, &params), XGL_OK);
    input.resize(payload.size() + XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE);
    size_t length = 0U;
    ASSERT_EQ(xgl_frame_serialize(input.data(), input.size(), &value, &length),
              XGL_OK);
    std::vector<uint8_t> parser_storage(length);
    ASSERT_EQ(
        xgl_parser_init(&parser, parser_storage.data(), parser_storage.size()),
        XGL_OK);

    while (!input.empty()) {
        ASSERT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 1U, 10U,
                                           XGL_DATALINK_RX_CHUNK_SIZE),
                  XGL_OK);
    }
    EXPECT_EQ(statistics.rx_packets, 0U);
    EXPECT_EQ(statistics.rx_errors, 1U);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_INVALID_FRAME);
}

TEST_F(XglCoverageDelivery,
       DeliveryPropagatesAdmissionAndAllowsAbsentReceiver) {
    xgl_frame_interface_t upper = {};
    context.upper_layer = &upper;
    context.stats = nullptr;
    EXPECT_EQ(xgl_datalink_process_frame(&context, frame, frame_size), XGL_OK);
    upper.receive = [](void*, xgl_handle_t,
                       const xgl_frame_rx_message_t* message) {
        EXPECT_NE(message->view, nullptr);
        EXPECT_EQ(message->view->header.source_id, 2U);
        return XGL_ERR_BUSY;
    };
    EXPECT_EQ(xgl_datalink_process_frame(&context, frame, frame_size),
              XGL_ERR_BUSY);
    context.upper_layer = nullptr;
    EXPECT_EQ(xgl_datalink_process_frame(&context, frame, frame_size), XGL_OK);
}

TEST_F(XglCoverageDelivery, ParserDropsCorruptInputAndAcceptsTheNextFrame) {
    for (bool observers : {true, false}) {
        context.stats = observers ? &statistics : nullptr;
        input.assign(frame, frame + frame_size);
        input.back() ^= 1U;
        input.insert(input.end(), frame, frame + frame_size);
        while (!input.empty()) {
            EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 1, 10,
                                               XGL_DATALINK_RX_CHUNK_SIZE),
                      XGL_OK);
        }
        EXPECT_EQ(parser.cache_len, 0U);
    }
    EXPECT_EQ(statistics.rx_errors, 1U);
    EXPECT_EQ(statistics.rx_packets, 1U);
}

TEST_F(XglCoverageDelivery, LayerInterfaceRejectsMissingFrameAndPhy) {
    xgl_frame_interface_t interface = {};
    xgl_frame_tx_message_t message = {};
    xgl_frame_t value = {};
    EXPECT_EQ(xgl_datalink_get_interface(nullptr, &interface),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_get_interface(&context, nullptr),
              XGL_ERR_NULL_POINTER);
    ASSERT_EQ(xgl_datalink_get_interface(&context, &interface), XGL_OK);
    EXPECT_EQ(interface.send(nullptr, nullptr, &message), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(interface.send(interface.ctx, nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(interface.send(interface.ctx, nullptr, &message),
              XGL_ERR_NULL_POINTER);
    message.frame = &value;
    EXPECT_EQ(interface.send(interface.ctx, nullptr, &message),
              XGL_ERR_NULL_POINTER);
    message.phy = &phy;
    EXPECT_EQ(interface.send(interface.ctx, nullptr, &message),
              XGL_ERR_INVALID_PARAM);
}

TEST_F(XglCoverageDelivery, InitRequiresStorageStatisticsAndUsableCapacity) {
    xgl_datalink_config_t config = {};
    EXPECT_EQ(xgl_datalink_init(nullptr, &config), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_init(&context, nullptr), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_init(&context, &config), XGL_ERR_NULL_POINTER);
    config.stats = &statistics;
    config.source_id = 1;
    EXPECT_EQ(xgl_datalink_init(&context, &config), XGL_OK);
    EXPECT_EQ(xgl_parser_init(&parser, cache, 0U), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_parser_init(&parser, cache, sizeof(cache)), XGL_OK);
    EXPECT_EQ(parser.cache, cache);
    EXPECT_EQ(context.stats, &statistics);
}

TEST_F(XglCoverageDelivery, SendValidatesPointersBeforeAllocatingStorage) {
    xgl_frame_t value = {};
    EXPECT_EQ(xgl_datalink_send(nullptr, &phy, &value), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_send(&context, nullptr, &value),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_send(&context, &phy, nullptr), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_send(&context, &phy, &value), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(statistics.tx_packets, 0U);
}

TEST_F(XglCoverageDelivery, NetworkInterfaceValidatesBorrowedMessages) {
    xgl_network_ctx_t network = {};
    network.local_id = 1;
    xgl_packet_interface_t packets = {};
    xgl_frame_interface_t frames = {};
    xgl_frame_rx_message_t message = {};
    EXPECT_EQ(xgl_network_get_interfaces(nullptr, &packets, &frames),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_network_get_interfaces(&network, nullptr, &frames),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_network_get_interfaces(&network, &packets, nullptr),
              XGL_ERR_NULL_POINTER);
    ASSERT_EQ(xgl_network_get_interfaces(&network, &packets, &frames), XGL_OK);
    EXPECT_EQ(frames.receive(nullptr, nullptr, &message), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(frames.receive(frames.ctx, nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(frames.receive(frames.ctx, nullptr, &message),
              XGL_ERR_NULL_POINTER);
    message.frame_buf = frame;
    message.frame_len = frame_size;
    EXPECT_EQ(frames.receive(frames.ctx, nullptr, &message), XGL_OK);
    xgl_packet_interface_t upper = {};
    network.upper_layer = &upper;
    EXPECT_EQ(frames.receive(frames.ctx, nullptr, &message), XGL_OK);
}

TEST_F(XglCoverageDelivery, NetworkRejectsMissingOrInvalidFrameViews) {
    xgl_network_ctx_t network = {};
    network.local_id = 1;
    xgl_wire_frame_view_t view = {};
    EXPECT_EQ(xgl_network_receive(nullptr, nullptr, frame, frame_size),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_network_receive(&network, nullptr, nullptr, frame_size),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_network_receive_view(nullptr, nullptr, &view),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_network_receive_view(&network, nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_network_receive_view(&network, nullptr, &view),
              XGL_ERR_NULL_POINTER);
    frame[frame_size - 1] ^= 1U;
    EXPECT_EQ(xgl_network_receive(&network, nullptr, frame, frame_size),
              XGL_ERR_CRC_FAILED);
    frame[frame_size - 1] ^= 1U;
    ASSERT_EQ(xgl_wire_decode_frame(&view, frame, frame_size, nullptr), XGL_OK);
    network.auth_required = true;
    EXPECT_EQ(xgl_network_receive_view(&network, nullptr, &view),
              XGL_ERR_INVALID_FRAME);
}

}  // namespace
