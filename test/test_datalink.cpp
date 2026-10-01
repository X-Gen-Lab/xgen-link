/**
 * \file            test_datalink.cpp
 * \brief           Unit tests for data link layer
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_datalink_metadata.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_config.h>

#include <cstdlib>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vector>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>
#include <xgen/memory/libc_allocator.h>

#include "test_security_helpers.h"

using ::testing::_;
using ::testing::Return;

/*---------------------------------------------------------------------------*/
/* Mock PHY Operations                                                       */
/*---------------------------------------------------------------------------*/

class MockPhyOps {
  public:
    MOCK_METHOD(xgl_error_t, tx,
                (const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(xgl_error_t, rx,
                (uint8_t* buffer, size_t* len, void* user_data));
};

static MockPhyOps* g_mock_phy = nullptr;

TEST(XglDatalinkMetadataTest, RejectsDuplicateSessionExtensions) {
    uint8_t bytes[64] = {};
    uint8_t value[XGL_SESSION_EXT_VALUE_SIZE] = {};
    size_t value_len = 0;
    ASSERT_EQ(xgl_wire_encode_session_ext_value(value, sizeof(value), 9, 0,
                                                &value_len),
              XGL_OK);
    size_t offset = XGL_WIRE_BASE_HEADER_SIZE;
    for (int i = 0; i < 2; ++i) {
        size_t written = 0;
        ASSERT_EQ(xgl_wire_encode_ext(bytes + offset, sizeof(bytes) - offset,
                                      XGL_WIRE_EXT_SESSION, value, value_len,
                                      &written),
                  XGL_OK);
        offset += written;
    }
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = static_cast<uint8_t>(offset);
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1;
    header.target_id = 2;
    ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header), XGL_OK);
    xgb_serialize_u16_le(bytes + offset, xgcrc_crc16_modbus(bytes, offset));
    xgl_datalink_rx_metadata_t metadata = {};
    EXPECT_EQ(xgl_datalink_decode_rx_metadata(bytes, offset + XGL_CRC16_SIZE,
                                              &metadata),
              XGL_ERR_INVALID_FRAME);
}

static xgl_error_t mock_phy_tx(const uint8_t* data, size_t len,
                               void* user_data) {
    return g_mock_phy->tx(data, len, user_data);
}

static xgl_error_t mock_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    return g_mock_phy->rx(buffer, len, user_data);
}

struct CountingAllocatorState {
    size_t alloc_count = 0;
    size_t free_count = 0;
};

static CountingAllocatorState* g_counting_allocator_state = nullptr;

static void* counting_malloc(void* user, size_t size) {
    if (g_counting_allocator_state != nullptr) {
        g_counting_allocator_state->alloc_count++;
    }
    return std::malloc(size);
}

static void counting_free(void* user, void* ptr) {
    if (ptr != nullptr && g_counting_allocator_state != nullptr) {
        g_counting_allocator_state->free_count++;
    }
    std::free(ptr);
}

static xgl_error_t datalink_test_auth_sign(const xgl_auth_input_t* input,
                                           uint8_t* tag, size_t tag_capacity,
                                           size_t* tag_len, void* user_data) {
    const uint32_t key_id = input->key_id;
    const uint8_t* aad = input->aad;
    const size_t aad_len = input->aad_len;
    const uint8_t* payload = input->payload;
    const size_t payload_len = input->payload_len;
    (void)user_data;
    if (tag == nullptr || tag_len == nullptr || tag_capacity < 4U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint32_t acc = key_id;
    for (size_t i = 0; i < aad_len; ++i) {
        acc = (acc * 33U) ^ aad[i];
    }
    for (size_t i = 0; i < payload_len; ++i) {
        acc = (acc * 33U) ^ payload[i];
    }

    xgb_serialize_u32_le(tag, acc);
    *tag_len = 4U;
    return XGL_OK;
}

static xgl_error_t datalink_test_auth_verify(const xgl_auth_input_t* input,
                                             const uint8_t* tag, size_t tag_len,
                                             bool* valid, void* user_data) {
    (void)user_data;
    uint8_t expected[4] = {};
    size_t expected_len = 0;
    xgl_error_t err = datalink_test_auth_sign(input, expected, sizeof(expected),
                                              &expected_len, nullptr);
    if (err != XGL_OK) {
        return err;
    }
    *valid = tag_len == expected_len &&
             std::memcmp(tag, expected, expected_len) == 0;
    return XGL_OK;
}

struct DatalinkUpperSpy {
    int receive_count = 0;
};

static xgl_error_t
datalink_upper_receive_spy(void* ctx, xgl_handle_t handle,
                           const xgl_frame_rx_message_t* data) {
    (void)handle;
    if (ctx == nullptr || data == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    auto* spy = static_cast<DatalinkUpperSpy*>(ctx);
    spy->receive_count++;
    return XGL_OK;
}

TEST(XglDatalinkMetadataTest, SeparateParsersPreserveInterleavedPhyFrames) {
    struct Input {
        uint8_t frame[64] = {};
        size_t size = 0;
        size_t offset = 0;
        size_t max_requested = 0;
    } inputs[2];

    for (size_t i = 0; i < 2; ++i) {
        const uint8_t payload = static_cast<uint8_t>(i + 20);
        xgl_frame_params_t params = {};
        params.source_id = static_cast<uint16_t>(i + 2);
        params.target_id = 1;
        params.payload = &payload;
        params.payload_len = 1;
        xgl_frame_t frame = {};
        ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
        ASSERT_EQ(xgl_frame_serialize(inputs[i].frame, sizeof(inputs[i].frame),
                                      &frame, &inputs[i].size),
                  XGL_OK);
    }
    auto read = [](uint8_t* output, size_t* size, void* ctx) {
        auto* input = static_cast<Input*>(ctx);
        input->max_requested =
            *size > input->max_requested ? *size : input->max_requested;
        const size_t remaining = input->size - input->offset;
        if (*size > remaining) {
            *size = remaining;
        }
        std::memcpy(output, input->frame + input->offset, *size);
        input->offset += *size;
        return XGL_OK;
    };
    xgl_phy_ops_t phys[2] = {};
    uint8_t caches[2][64] = {};
    xgl_parser_t parsers[2] = {};
    std::vector<uint8_t> received;
    xgl_frame_interface_t upper = {};
    upper.ctx = &received;
    upper.receive = [](void* ctx, xgl_handle_t,
                       const xgl_frame_rx_message_t* data) {
        const auto* message = data;
        if (message->view == nullptr || message->view->payload_len != 1) {
            return XGL_ERR_INVALID_FRAME;
        }
        static_cast<std::vector<uint8_t>*>(ctx)->push_back(
            message->view->payload[0]);
        return XGL_OK;
    };
    xgl_layer_stats_t stats = {};
    xgl_datalink_ctx_t ctx = {};
    ctx.stats = &stats;
    ctx.upper_layer = &upper;
    for (size_t i = 0; i < 2; ++i) {
        phys[i].rx = read;
        phys[i].user_data = &inputs[i];
        ASSERT_EQ(xgl_parser_init(&parsers[i], caches[i], sizeof(caches[i])),
                  XGL_OK);
        ASSERT_EQ(
            xgl_datalink_poll_parser(&ctx, &parsers[i], &phys[i], 0, 1000, 13),
            XGL_OK);
    }
    EXPECT_TRUE(received.empty());
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(
            xgl_datalink_poll_parser(&ctx, &parsers[i], &phys[i], 1, 1000, 14),
            XGL_OK);
        EXPECT_EQ(inputs[i].max_requested, 14U);
    }
    EXPECT_EQ(received, (std::vector<uint8_t>{20, 21}));
    EXPECT_EQ(stats.rx_errors, 0U);
    EXPECT_EQ(stats.rx_packets, 2U);
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglDatalinkTest : public ::testing::Test {
  protected:
    void SetUp() override {
        g_mock_phy = &mock_phy;

        /* Initialize PHY operations */
        phy_ops.tx = mock_phy_tx;
        phy_ops.rx = mock_phy_rx;
        phy_ops.user_data = nullptr;

        /* Initialize statistics */
        std::memset(&stats, 0, sizeof(stats));
        rx_header_crc_errors = 0;
        rx_crc16_errors = 0;

        /* Initialize datalink context */
        xgl_datalink_config_t config = {};
        config.source_id = SOURCE_ID;
        config.stats = &stats;
        config.rx_header_crc_errors = &rx_header_crc_errors;
        config.rx_crc16_errors = &rx_crc16_errors;
        config.upper_layer = nullptr;
        config.error_callback = nullptr;
        config.callback_user_data = nullptr;
        config.allocator = xgm_allocator_libc();
        xgl_datalink_init(&ctx, &config);
    }

    void TearDown() override {
        g_mock_phy = nullptr;
    }

    MockPhyOps mock_phy;
    xgl_phy_ops_t phy_ops;
    xgl_layer_stats_t stats;
    uint64_t rx_header_crc_errors;
    uint64_t rx_crc16_errors;
    xgl_datalink_ctx_t ctx;

    static constexpr uint8_t SOURCE_ID = 0x01;
    static constexpr uint8_t TARGET_ID = 0x02;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, InitSuccess) {
    xgl_datalink_ctx_t test_ctx;
    xgl_layer_stats_t test_stats = {0};
    uint64_t header_crc = 0, crc16 = 0;

    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &test_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    xgl_error_t err = xgl_datalink_init(&test_ctx, &config);

    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(test_ctx.source_id, SOURCE_ID);
}

TEST_F(XglDatalinkTest, InitNullPointer) {
    xgl_layer_stats_t test_stats = {0};
    uint64_t header_crc = 0, crc16 = 0;

    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &test_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;

    EXPECT_EQ(xgl_datalink_init(nullptr, &config), XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Frame Transmission Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, SendFrameSuccess) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};

    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = 0x01;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;

    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _)).WillOnce(Return(XGL_OK));

    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
}

TEST_F(XglDatalinkTest, SendFrameNullPointer) {
    xgl_frame_t frame;

    EXPECT_EQ(xgl_datalink_send(nullptr, &phy_ops, &frame),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_send(&ctx, nullptr, &frame), XGL_ERR_NULL_POINTER);
}

TEST_F(XglDatalinkTest, InplaceCapacityPrecedesPayloadAddressValidation) {
    const uint8_t payload = 0x35;
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.payload = &payload;
    params.payload_len = 1U;
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    uint8_t destination[1] = {};
    EXPECT_CALL(mock_phy, tx(_, _, _)).Times(0);

    EXPECT_EQ(xgl_datalink_send_inplace(&ctx, &phy_ops, &frame, destination,
                                        sizeof(destination),
                                        XGL_WIRE_BASE_HEADER_SIZE),
              XGL_ERR_BUFFER_TOO_SMALL);
}

TEST_F(XglDatalinkTest, SendFramePhyError) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA};

    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = 0x01;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;

    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _)).WillOnce(Return(XGL_ERR_TX_FAILED));

    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_ERR_TX_FAILED);
    EXPECT_EQ(stats.tx_errors, 1);
}

TEST_F(XglDatalinkTest, SendLargeFrameUsesConfiguredAllocator) {
    CountingAllocatorState allocator_state;
    g_counting_allocator_state = &allocator_state;
    xgm_allocator_t allocator = {};
    allocator.ctx = &allocator_state;
    allocator.alloc = counting_malloc;
    allocator.free = counting_free;

    xgl_datalink_ctx_t large_ctx;
    xgl_layer_stats_t large_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &large_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    config.allocator = &allocator;
    ASSERT_EQ(xgl_datalink_init(&large_ctx, &config), XGL_OK);

    std::vector<uint8_t> payload(512U, 0xA5);
    xgl_frame_t frame;
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = 0x01;
    params.payload = payload.data();
    params.payload_len = payload.size();
    params.reliable = false;
    params.priority = 0;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _)).WillOnce(Return(XGL_OK));

    EXPECT_EQ(xgl_datalink_send(&large_ctx, &phy_ops, &frame), XGL_OK);
    EXPECT_EQ(allocator_state.alloc_count, 1U);
    EXPECT_EQ(allocator_state.free_count, 1U);

    g_counting_allocator_state = nullptr;
}

TEST_F(XglDatalinkTest,
       SmallFrameReleasesAllocatorBufferAfterSuccessAndFailure) {
    CountingAllocatorState allocator_state;
    g_counting_allocator_state = &allocator_state;
    xgm_allocator_t allocator = {};
    allocator.ctx = &allocator_state;
    allocator.alloc = counting_malloc;
    allocator.free = counting_free;
    ctx.allocator = &allocator;
    const uint8_t payload = 0xA5U;
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.payload = &payload;
    params.payload_len = sizeof(payload);
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK))
        .WillOnce(Return(XGL_ERR_TX_FAILED));

    EXPECT_EQ(xgl_datalink_send(&ctx, &phy_ops, &frame), XGL_OK);
    EXPECT_EQ(allocator_state.alloc_count, 1U);
    EXPECT_EQ(allocator_state.free_count, 1U);
    EXPECT_EQ(xgl_datalink_send(&ctx, &phy_ops, &frame), XGL_ERR_TX_FAILED);
    EXPECT_EQ(allocator_state.alloc_count, 2U);
    EXPECT_EQ(allocator_state.free_count, 2U);
    EXPECT_EQ(stats.tx_packets, 1U);
    EXPECT_EQ(stats.tx_errors, 1U);
    g_counting_allocator_state = nullptr;
}

TEST_F(XglDatalinkTest, SendFrameAuthenticatesWhenConfigured) {
    xgl_auth_provider_t provider = {};
    provider.sign = datalink_test_auth_sign;
    provider.verify = datalink_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    xgl_datalink_ctx_t auth_ctx;
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &auth_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    xgl_security_ctx_t security = {};
    ASSERT_EQ(xgl_security_init(&security, SOURCE_ID, true, &provider), XGL_OK);
    config.security = &security;
    config.allocator = xgm_allocator_libc();
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 0;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    auto trusted = test_session_config(TARGET_ID);
    ASSERT_EQ(xgl_security_session_install(auth_ctx.security, &trusted),
              XGL_OK);
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce([&provider](const uint8_t* data, size_t len, void*) {
            xgl_wire_header_t header = {};
            EXPECT_EQ(xgl_wire_decode_header(&header, data, len), XGL_OK);
            EXPECT_NE(header.flags & XGL_WIRE_FLAG_AUTHENTICATED, 0);
            EXPECT_GT(header.header_len, XGL_WIRE_BASE_HEADER_SIZE);

            bool valid = false;
            EXPECT_EQ(test_verify_trusted_frame(
                          data, len - XGL_CRC16_SIZE, header.header_len,
                          header.payload_len, 7, &provider, &valid),
                      XGL_OK);
            EXPECT_TRUE(valid);
            return XGL_OK;
        });

    EXPECT_EQ(xgl_datalink_send(&auth_ctx, &phy_ops, &frame), XGL_OK);
    EXPECT_EQ(auth_stats.tx_packets, 1U);
}

TEST_F(XglDatalinkTest, ProcessFrameLeavesEndToEndAuthenticationToNetwork) {
    xgl_auth_provider_t provider = {};
    provider.sign = datalink_test_auth_sign;
    provider.verify = datalink_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    xgl_datalink_ctx_t auth_ctx;
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &auth_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    xgl_security_ctx_t security = {};
    ASSERT_EQ(xgl_security_init(&security, SOURCE_ID, true, &provider), XGL_OK);
    config.security = &security;
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 0;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(test_serialize_trusted_frame(encoded, sizeof(encoded), &frame, 7,
                                           &provider, &encoded_len),
              XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, encoded, encoded_len), XGL_OK);
    encoded[header.header_len] ^= 0x01U;
    uint16_t crc = xgcrc_crc16_modbus(encoded, encoded_len - XGL_CRC16_SIZE);
    xgb_serialize_u16_le(&encoded[encoded_len - XGL_CRC16_SIZE], crc);

    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len),
              XGL_OK);
    EXPECT_EQ(auth_stats.rx_errors, 0U);
    EXPECT_EQ(auth_stats.rx_packets, 1U);
}

TEST_F(XglDatalinkTest, ProcessFrameDoesNotCommitEndpointReplayState) {
    xgl_auth_provider_t provider = {};
    provider.sign = datalink_test_auth_sign;
    provider.verify = datalink_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    xgl_datalink_ctx_t auth_ctx;
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &auth_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    xgl_security_ctx_t security = {};
    ASSERT_EQ(xgl_security_init(&security, SOURCE_ID, true, &provider), XGL_OK);
    config.security = &security;
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(test_serialize_trusted_frame(encoded, sizeof(encoded), &frame, 7,
                                           &provider, &encoded_len),
              XGL_OK);

    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len),
              XGL_OK);
    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len),
              XGL_OK);
    EXPECT_EQ(auth_stats.rx_packets, 2U);
    EXPECT_EQ(auth_stats.rx_dropped, 0U);
}

TEST_F(XglDatalinkTest, ProcessFrameDeliversWireValidReliableCopiesUpstream) {
    xgl_auth_provider_t provider = {};
    provider.sign = datalink_test_auth_sign;
    provider.verify = datalink_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    DatalinkUpperSpy upper_spy;
    xgl_frame_interface_t upper_layer = {};
    xgl_frame_interface_init(&upper_layer, &upper_spy, nullptr,
                             datalink_upper_receive_spy);

    xgl_datalink_ctx_t auth_ctx;
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &auth_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = &upper_layer;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    xgl_security_ctx_t security = {};
    ASSERT_EQ(xgl_security_init(&security, SOURCE_ID, true, &provider), XGL_OK);
    config.security = &security;
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 0;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(test_serialize_trusted_frame(encoded, sizeof(encoded), &frame, 7,
                                           &provider, &encoded_len),
              XGL_OK);

    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len),
              XGL_OK);
    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len),
              XGL_OK);
    EXPECT_EQ(upper_spy.receive_count, 2);
    EXPECT_EQ(auth_stats.rx_dropped, 0U);
}

TEST_F(XglDatalinkTest, ProcessFrameHasNoImplicitEndpointAuthenticationPolicy) {
    xgl_auth_provider_t provider = {};
    provider.sign = datalink_test_auth_sign;
    provider.verify = datalink_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    xgl_datalink_ctx_t optional_auth_ctx;
    xgl_layer_stats_t optional_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {};
    config.source_id = SOURCE_ID;
    config.stats = &optional_stats;
    config.rx_header_crc_errors = &header_crc;
    config.rx_crc16_errors = &crc16;
    config.upper_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    xgl_security_ctx_t security = {};
    ASSERT_EQ(xgl_security_init(&security, SOURCE_ID, false, &provider),
              XGL_OK);
    config.security = &security;
    ASSERT_EQ(xgl_datalink_init(&optional_auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 0;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(test_serialize_trusted_frame(encoded, sizeof(encoded), &frame, 7,
                                           &provider, &encoded_len),
              XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, encoded, encoded_len), XGL_OK);
    encoded[header.header_len] ^= 0x01U;
    uint16_t frame_crc =
        xgcrc_crc16_modbus(encoded, encoded_len - XGL_CRC16_SIZE);
    xgb_serialize_u16_le(&encoded[encoded_len - XGL_CRC16_SIZE], frame_crc);

    EXPECT_EQ(
        xgl_datalink_process_frame(&optional_auth_ctx, encoded, encoded_len),
        XGL_OK);
    EXPECT_EQ(optional_stats.rx_packets, 1U);
    EXPECT_EQ(optional_stats.rx_errors, 0U);
}

TEST_F(XglDatalinkTest, RxMetadataDecodesAuthenticatedSessionFrame) {
    xgl_auth_provider_t provider = {};
    provider.sign = datalink_test_auth_sign;
    provider.verify = datalink_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;

    uint8_t session_value[XGL_SESSION_EXT_VALUE_SIZE] = {};
    size_t session_value_len = 0U;
    ASSERT_EQ(xgl_wire_encode_session_ext_value(
                  session_value, sizeof(session_value), 0x01020304U,
                  0x1122334455667788ULL, &session_value_len),
              XGL_OK);
    uint8_t session_ext[XGL_SESSION_EXT_SIZE] = {};
    size_t session_ext_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(session_ext, sizeof(session_ext),
                                  XGL_WIRE_EXT_SESSION, session_value,
                                  session_value_len, &session_ext_len),
              XGL_OK);

    xgl_frame_t frame = {};
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.connection_id = 0x10203040U;
    params.packet_number = 77U;
    params.extensions = session_ext;
    params.extensions_len = session_ext_len;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.ttl = 8U;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0U;
    ASSERT_EQ(test_serialize_trusted_frame(encoded, sizeof(encoded), &frame, 7U,
                                           &provider, &encoded_len),
              XGL_OK);

    xgl_datalink_rx_metadata_t metadata = {};
    ASSERT_EQ(xgl_datalink_decode_rx_metadata(encoded, encoded_len, &metadata),
              XGL_OK);

    EXPECT_EQ(metadata.frame.header.source_id, SOURCE_ID);
    EXPECT_EQ(metadata.frame.header.target_id, TARGET_ID);
    EXPECT_EQ(metadata.frame.header.connection_id, 0x10203040U);
    EXPECT_EQ(metadata.frame.header.packet_number, 77U);
    EXPECT_EQ(metadata.frame.auth_tag_len, 4U);
    EXPECT_TRUE(metadata.frame.has_security_ext);
    EXPECT_TRUE(metadata.frame.authenticated);
    EXPECT_TRUE(metadata.frame.authenticated);
    EXPECT_EQ(metadata.frame.auth_key_id, 7U);
    EXPECT_EQ(metadata.frame.session_epoch, 0x01020304U);
    EXPECT_EQ(metadata.frame.payload_len, sizeof(payload));
}

TEST_F(XglDatalinkTest,
       RxMetadataReportsAuthenticationKeyWithoutApplyingEndpointPolicy) {
    xgl_auth_provider_t provider = {};
    provider.sign = datalink_test_auth_sign;
    provider.verify = datalink_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    xgl_frame_t frame = {};
    const uint8_t payload[] = {0x01};
    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0U;
    ASSERT_EQ(test_serialize_trusted_frame(encoded, sizeof(encoded), &frame, 7U,
                                           &provider, &encoded_len),
              XGL_OK);

    xgl_datalink_rx_metadata_t metadata = {};
    EXPECT_EQ(xgl_datalink_decode_rx_metadata(encoded, encoded_len, &metadata),
              XGL_OK);
    EXPECT_EQ(metadata.frame.auth_key_id, 7U);
}

/*---------------------------------------------------------------------------*/
/* Statistics Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, StatisticsTracking) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};

    xgl_frame_params_t params = {};
    params.source_id = SOURCE_ID;
    params.target_id = TARGET_ID;
    params.data_type = 0x01;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;

    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _)).WillOnce(Return(XGL_OK));

    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_OK);

    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
    EXPECT_EQ(stats.tx_errors, 0);
}
