/**
 * \file            test_datalink.cpp
 * \brief           Unit tests for data link layer
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_datalink_metadata.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_transport.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/xgl_config.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/internal/xgl_crc.h>
#include <xgl/internal/xgl_serialize.h>
#include <cstring>
#include <cstdlib>
#include <vector>

using ::testing::_;
using ::testing::Return;

/*---------------------------------------------------------------------------*/
/* Mock PHY Operations                                                       */
/*---------------------------------------------------------------------------*/

class MockPhyOps {
public:
    MOCK_METHOD(xgl_error_t, tx, (const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(xgl_error_t, rx, (uint8_t* buffer, size_t* len, void* user_data));
};

static MockPhyOps* g_mock_phy = nullptr;

static xgl_error_t mock_phy_tx(const uint8_t* data, size_t len, void* user_data) {
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

static void* counting_malloc(size_t size) {
    if (g_counting_allocator_state != nullptr) {
        g_counting_allocator_state->alloc_count++;
    }
    return std::malloc(size);
}

static void counting_free(void* ptr) {
    if (ptr != nullptr && g_counting_allocator_state != nullptr) {
        g_counting_allocator_state->free_count++;
    }
    std::free(ptr);
}

static xgl_error_t datalink_test_auth_sign(uint32_t key_id,
                                           const uint8_t* aad,
                                           size_t aad_len,
                                           const uint8_t* payload,
                                           size_t payload_len,
                                           uint8_t* tag,
                                           size_t tag_capacity,
                                           size_t* tag_len,
                                           void* user_data) {
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

    xgl_serialize_u32_le(tag, acc);
    *tag_len = 4U;
    return XGL_OK;
}

static xgl_error_t datalink_test_auth_verify(uint32_t key_id,
                                             const uint8_t* aad,
                                             size_t aad_len,
                                             const uint8_t* payload,
                                             size_t payload_len,
                                             const uint8_t* tag,
                                             size_t tag_len,
                                             bool* valid,
                                             void* user_data) {
    (void)user_data;
    uint8_t expected[4] = {};
    size_t expected_len = 0;
    xgl_error_t err = datalink_test_auth_sign(key_id,
                                              aad,
                                              aad_len,
                                              payload,
                                              payload_len,
                                              expected,
                                              sizeof(expected),
                                              &expected_len,
                                              nullptr);
    if (err != XGL_OK) {
        return err;
    }
    *valid = tag_len == expected_len && std::memcmp(tag, expected, expected_len) == 0;
    return XGL_OK;
}

struct DatalinkUpperSpy {
    int receive_count = 0;
};

static xgl_error_t datalink_upper_receive_spy(void* ctx,
                                              xgl_handle_t handle,
                                              void* data) {
    (void)handle;
    if (ctx == nullptr || data == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    auto* spy = static_cast<DatalinkUpperSpy*>(ctx);
    spy->receive_count++;
    return XGL_OK;
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
        xgl_datalink_config_t config = {
            .rx_cache = rx_cache,
            .rx_cache_size = sizeof(rx_cache),
            .source_id = SOURCE_ID,
            .stats = &stats,
            .rx_header_crc_errors = &rx_header_crc_errors,
            .rx_crc16_errors = &rx_crc16_errors,
            .upper_layer = nullptr,
            .error_callback = nullptr,
            .callback_user_data = nullptr
        };
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
    uint8_t rx_cache[512];

    static constexpr uint8_t SOURCE_ID = 0x01;
    static constexpr uint8_t TARGET_ID = 0x02;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, InitSuccess) {
    xgl_datalink_ctx_t test_ctx;
    uint8_t cache[256];
    xgl_layer_stats_t test_stats = {0};
    uint64_t header_crc = 0, crc16 = 0;

    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &test_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr
    };
    xgl_error_t err = xgl_datalink_init(&test_ctx, &config);

    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(test_ctx.source_id, SOURCE_ID);
}

TEST_F(XglDatalinkTest, InitNullPointer) {
    uint8_t cache[256];
    xgl_layer_stats_t test_stats = {0};
    uint64_t header_crc = 0, crc16 = 0;

    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &test_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr
    };

    EXPECT_EQ(xgl_datalink_init(nullptr, &config), XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Frame Transmission Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, SendFrameSuccess) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};

    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = 0x01,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };

    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK));

    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
}

TEST_F(XglDatalinkTest, SendFrameNullPointer) {
    xgl_frame_t frame;

    EXPECT_EQ(xgl_datalink_send(nullptr, &phy_ops, &frame),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_send(&ctx, nullptr, &frame),
              XGL_ERR_NULL_POINTER);
}

TEST_F(XglDatalinkTest, SendFramePhyError) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA};

    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = 0x01,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };

    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_ERR_TX_FAILED));

    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_ERR_TX_FAILED);
    EXPECT_EQ(stats.tx_errors, 1);
}

TEST_F(XglDatalinkTest, SendLargeFrameUsesConfiguredAllocator) {
    CountingAllocatorState allocator_state;
    g_counting_allocator_state = &allocator_state;
    xgl_allocator_t allocator = {
        .malloc = counting_malloc,
        .free = counting_free,
        .user_data = &allocator_state
    };

    xgl_datalink_ctx_t large_ctx;
    uint8_t cache[1024];
    xgl_layer_stats_t large_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &large_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr,
        .allocator = &allocator
    };
    ASSERT_EQ(xgl_datalink_init(&large_ctx, &config), XGL_OK);

    std::vector<uint8_t> payload(XGL_DATALINK_STACK_BUFFER_SIZE, 0xA5);
    xgl_frame_t frame;
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = 0x01,
        .payload = payload.data(),
        .payload_len = payload.size(),
        .reliable = false,
        .priority = 0
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK));

    EXPECT_EQ(xgl_datalink_send(&large_ctx, &phy_ops, &frame), XGL_OK);
    EXPECT_EQ(allocator_state.alloc_count, 1U);
    EXPECT_EQ(allocator_state.free_count, 1U);

    g_counting_allocator_state = nullptr;
}

TEST_F(XglDatalinkTest, SendFrameAuthenticatesWhenConfigured) {
    xgl_auth_provider_t provider = {
        .sign = datalink_test_auth_sign,
        .verify = datalink_test_auth_verify,
        .tag_len = 4,
        .user_data = nullptr
    };
    xgl_datalink_ctx_t auth_ctx;
    uint8_t cache[512] = {};
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &auth_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr,
        .auth_required = true,
        .auth_key_id = 7,
        .auth_provider = &provider
    };
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = XGL_PACKET_TYPE_DATA,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true,
        .priority = 0
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce([&provider](const uint8_t* data, size_t len, void*) {
            xgl_wire_header_t header = {};
            EXPECT_EQ(xgl_wire_decode_header(&header, data, len), XGL_OK);
            EXPECT_NE(header.flags & XGL_WIRE_FLAG_AUTHENTICATED, 0);
            EXPECT_GT(header.header_len, XGL_WIRE_BASE_HEADER_SIZE);

            bool valid = false;
            EXPECT_EQ(xgl_wire_verify_auth_trailer(data,
                                                   len - XGL_CRC16_SIZE,
                                                   header.header_len,
                                                   header.payload_len,
                                                   7,
                                                   &provider,
                                                   &valid),
                      XGL_OK);
            EXPECT_TRUE(valid);
            return XGL_OK;
        });

    EXPECT_EQ(xgl_datalink_send(&auth_ctx, &phy_ops, &frame), XGL_OK);
    EXPECT_EQ(auth_stats.tx_packets, 1U);
}

TEST_F(XglDatalinkTest, ProcessFrameRejectsTamperedAuthenticatedPayload) {
    xgl_auth_provider_t provider = {
        .sign = datalink_test_auth_sign,
        .verify = datalink_test_auth_verify,
        .tag_len = 4,
        .user_data = nullptr
    };
    xgl_datalink_ctx_t auth_ctx;
    uint8_t cache[512] = {};
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &auth_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr,
        .auth_required = true,
        .auth_key_id = 7,
        .auth_provider = &provider
    };
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = XGL_PACKET_TYPE_DATA,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true,
        .priority = 0
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded,
                                                sizeof(encoded),
                                                &frame,
                                                7,
                                                &provider,
                                                &encoded_len),
              XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, encoded, encoded_len), XGL_OK);
    encoded[header.header_len] ^= 0x01U;
    uint16_t crc = xgl_crc16_modbus(encoded, encoded_len - XGL_CRC16_SIZE);
    xgl_serialize_u16_le(&encoded[encoded_len - XGL_CRC16_SIZE], crc);

    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(auth_stats.rx_errors, 1U);
    EXPECT_EQ(auth_stats.rx_packets, 0U);
}

TEST_F(XglDatalinkTest, ProcessFrameRejectsAuthenticatedUnreliableReplay) {
    xgl_auth_provider_t provider = {
        .sign = datalink_test_auth_sign,
        .verify = datalink_test_auth_verify,
        .tag_len = 4,
        .user_data = nullptr
    };
    xgl_datalink_ctx_t auth_ctx;
    uint8_t cache[512] = {};
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &auth_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr,
        .auth_required = true,
        .auth_key_id = 7,
        .auth_provider = &provider
    };
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = XGL_PACKET_TYPE_DATA,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded,
                                                sizeof(encoded),
                                                &frame,
                                                7,
                                                &provider,
                                                &encoded_len),
              XGL_OK);

    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len), XGL_OK);
    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(auth_stats.rx_packets, 1U);
    EXPECT_EQ(auth_stats.rx_dropped, 1U);
}

TEST_F(XglDatalinkTest, ProcessFrameAllowsAuthenticatedReliableDuplicateForTransportAck) {
    xgl_auth_provider_t provider = {
        .sign = datalink_test_auth_sign,
        .verify = datalink_test_auth_verify,
        .tag_len = 4,
        .user_data = nullptr
    };
    DatalinkUpperSpy upper_spy;
    xgl_layer_interface_t upper_layer = {};
    xgl_layer_interface_init(&upper_layer,
                             &upper_spy,
                             nullptr,
                             datalink_upper_receive_spy,
                             nullptr);

    xgl_datalink_ctx_t auth_ctx;
    uint8_t cache[512] = {};
    xgl_layer_stats_t auth_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &auth_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = &upper_layer,
        .error_callback = nullptr,
        .callback_user_data = nullptr,
        .auth_required = true,
        .auth_key_id = 7,
        .auth_provider = &provider
    };
    ASSERT_EQ(xgl_datalink_init(&auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = XGL_PACKET_TYPE_DATA,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true,
        .priority = 0
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded,
                                                sizeof(encoded),
                                                &frame,
                                                7,
                                                &provider,
                                                &encoded_len),
              XGL_OK);

    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len), XGL_OK);
    EXPECT_EQ(xgl_datalink_process_frame(&auth_ctx, encoded, encoded_len), XGL_OK);
    EXPECT_EQ(upper_spy.receive_count, 2);
    EXPECT_EQ(auth_stats.rx_dropped, 0U);
}

TEST_F(XglDatalinkTest, ProcessFrameVerifiesAuthenticatedFrameEvenWhenAuthOptional) {
    xgl_auth_provider_t provider = {
        .sign = datalink_test_auth_sign,
        .verify = datalink_test_auth_verify,
        .tag_len = 4,
        .user_data = nullptr
    };
    xgl_datalink_ctx_t optional_auth_ctx;
    uint8_t cache[512] = {};
    xgl_layer_stats_t optional_stats = {};
    uint64_t header_crc = 0;
    uint64_t crc16 = 0;
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &optional_stats,
        .rx_header_crc_errors = &header_crc,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr,
        .auth_required = false,
        .auth_key_id = 7,
        .auth_provider = &provider
    };
    ASSERT_EQ(xgl_datalink_init(&optional_auth_ctx, &config), XGL_OK);

    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = XGL_PACKET_TYPE_DATA,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true,
        .priority = 0
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded,
                                                sizeof(encoded),
                                                &frame,
                                                7,
                                                &provider,
                                                &encoded_len),
              XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, encoded, encoded_len), XGL_OK);
    encoded[header.header_len] ^= 0x01U;
    uint16_t frame_crc = xgl_crc16_modbus(encoded, encoded_len - XGL_CRC16_SIZE);
    xgl_serialize_u16_le(&encoded[encoded_len - XGL_CRC16_SIZE], frame_crc);

    EXPECT_EQ(xgl_datalink_process_frame(&optional_auth_ctx, encoded, encoded_len),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(optional_stats.rx_packets, 0U);
    EXPECT_EQ(optional_stats.rx_errors, 1U);
}

TEST_F(XglDatalinkTest, RxMetadataDecodesAuthenticatedSessionFrame) {
    xgl_auth_provider_t provider = {
        .sign = datalink_test_auth_sign,
        .verify = datalink_test_auth_verify,
        .tag_len = 4,
        .user_data = nullptr
    };

    uint8_t session_value[XGL_SESSION_EXT_VALUE_SIZE] = {};
    size_t session_value_len = 0U;
    ASSERT_EQ(xgl_wire_encode_session_ext_value(session_value,
                                                sizeof(session_value),
                                                0x01020304U,
                                                0x1122334455667788ULL,
                                                &session_value_len),
              XGL_OK);
    uint8_t session_ext[XGL_SESSION_EXT_SIZE] = {};
    size_t session_ext_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(session_ext,
                                  sizeof(session_ext),
                                  XGL_WIRE_EXT_SESSION,
                                  session_value,
                                  session_value_len,
                                  &session_ext_len),
              XGL_OK);

    xgl_frame_t frame = {};
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .connection_id = 0x10203040U,
        .packet_number = 77U,
        .extensions = session_ext,
        .extensions_len = session_ext_len,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true,
        .ttl = XGL_DEFAULT_TTL
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0U;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded,
                                                sizeof(encoded),
                                                &frame,
                                                7U,
                                                &provider,
                                                &encoded_len),
              XGL_OK);

    xgl_datalink_rx_metadata_t metadata = {};
    ASSERT_EQ(xgl_datalink_decode_rx_metadata(encoded,
                                              encoded_len,
                                              true,
                                              7U,
                                              &metadata),
              XGL_OK);

    EXPECT_EQ(metadata.header.source_id, SOURCE_ID);
    EXPECT_EQ(metadata.header.target_id, TARGET_ID);
    EXPECT_EQ(metadata.header.connection_id, 0x10203040U);
    EXPECT_EQ(metadata.header.packet_number, 77U);
    EXPECT_EQ(metadata.auth_tag_len, 4U);
    EXPECT_TRUE(metadata.has_security_ext);
    EXPECT_TRUE(metadata.authenticated);
    EXPECT_TRUE(metadata.should_verify_auth);
    EXPECT_EQ(metadata.auth_key_id, 7U);
    EXPECT_EQ(metadata.session_epoch, 0x01020304U);
    EXPECT_EQ(metadata.payload_len, sizeof(payload));
}

TEST_F(XglDatalinkTest, RxMetadataRejectsWrongRequiredAuthKey) {
    xgl_auth_provider_t provider = {
        .sign = datalink_test_auth_sign,
        .verify = datalink_test_auth_verify,
        .tag_len = 4,
        .user_data = nullptr
    };
    xgl_frame_t frame = {};
    const uint8_t payload[] = {0x01};
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    uint8_t encoded[256] = {};
    size_t encoded_len = 0U;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded,
                                                sizeof(encoded),
                                                &frame,
                                                7U,
                                                &provider,
                                                &encoded_len),
              XGL_OK);

    xgl_datalink_rx_metadata_t metadata = {};
    EXPECT_EQ(xgl_datalink_decode_rx_metadata(encoded,
                                              encoded_len,
                                              true,
                                              8U,
                                              &metadata),
              XGL_ERR_INVALID_FRAME);
}

/*---------------------------------------------------------------------------*/
/* Statistics Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, StatisticsTracking) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};

    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = 0x01,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };

    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);

    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK));

    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_OK);

    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
    EXPECT_EQ(stats.tx_errors, 0);
}
