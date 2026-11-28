/**
 * \file            test_send.cpp
 * \brief           Unit tests for send API
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_config.h"
#include "test_host_allocator.h"
#include <xgl/xgl_diagnostics.h>
#include <xgl/xgl_inplace.h>
#include <xgl/xgl_security.h>

#include <network/xgl_network.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>

#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <internal/xgl_allocator.h>
#include <vector>

#include "test_security_helpers.h"

/*---------------------------------------------------------------------------*/
/* Mock Physical Layer                                                       */
/*---------------------------------------------------------------------------*/

class SendTestPhy {
  public:
    MOCK_METHOD(xgl_error_t, tx,
                (const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(xgl_error_t, rx,
                (uint8_t * buffer, size_t* len, void* user_data));
};

/* Global mock instance for C callbacks */
static SendTestPhy* s_mock_phy = nullptr;
static constexpr size_t XGL_TEST_ZERO_COPY_APP_HEADER_LEN =
    XGL_FRAME_HEADER_SIZE + XGL_WIRE_EXT_HEADER_SIZE + 1U;

/* C callback wrappers */
static xgl_error_t mock_phy_tx(const uint8_t* data, size_t len,
                               void* user_data) {
    if (s_mock_phy) {
        return s_mock_phy->tx(data, len, user_data);
    }
    return XGL_OK;
}

static xgl_error_t mock_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    if (s_mock_phy) {
        return s_mock_phy->rx(buffer, len, user_data);
    }
    return XGL_OK;
}

static xgl_error_t send_test_auth_sign(const xgl_auth_input_t* input,
                                       uint8_t* tag, size_t tag_capacity,
                                       size_t* tag_len, void* user_data) {
    const uint32_t key_id = input->key_id;
    const uint8_t* aad = input->aad;
    const size_t aad_len = input->aad_len;
    const uint8_t* payload = input->payload;
    const size_t payload_len = input->payload_len;
    (void)user_data;
    if (tag == nullptr || tag_len == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    if ((aad == nullptr && aad_len > 0U) ||
        (payload == nullptr && payload_len > 0U)) {
        return XGL_ERR_NULL_POINTER;
    }
    if (tag_capacity < 8U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint32_t acc = key_id;
    for (size_t i = 0; i < aad_len; ++i) {
        acc = (acc * 33U) ^ aad[i];
    }
    for (size_t i = 0; i < payload_len; ++i) {
        acc = (acc * 33U) ^ payload[i];
    }
    for (size_t i = 0; i < 8U; ++i) {
        tag[i] = static_cast<uint8_t>((acc >> ((i % 4U) * 8U)) & 0xFFU);
    }
    *tag_len = 8U;
    return XGL_OK;
}

static xgl_error_t send_test_auth_verify(const xgl_auth_input_t* input,
                                         const uint8_t* tag, size_t tag_len,
                                         bool* valid, void* user_data) {
    if (tag == nullptr || valid == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    uint8_t expected[8] = {};
    size_t expected_len = 0;
    xgl_error_t err = send_test_auth_sign(input, expected, sizeof(expected),
                                          &expected_len, user_data);
    if (err != XGL_OK) {
        return err;
    }
    *valid = tag_len == expected_len &&
             std::memcmp(tag, expected, expected_len) == 0;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglSendTest : public ::testing::Test {
  protected:
    void SetUp() override {
        /* Create mock PHY */
        mock_phy = new SendTestPhy();
        s_mock_phy = mock_phy;

        /* Setup PHY operations */
        phy_ops.tx = mock_phy_tx;
        phy_ops.rx = mock_phy_rx;
        phy_ops.user_data = nullptr;

        /* Setup route */
        route.target_id = 2;
        route.phy = &phy_ops;
        route.max_frame_size = 256;
        route.read_freq_hz = 100;

        config = xgl_test_config();
        config.source_id = 1;
        config.route_table = &route;
        config.route_table_len = 1;

        /* Create and initialize instance */
        handle = xgl_test_init_allocated(&config);
        ASSERT_NE(handle, nullptr);
    }

    void TearDown() override {
        if (handle) {
            xgl_destroy(handle);
            handle = nullptr;
        }

        s_mock_phy = nullptr;
        delete mock_phy;
        mock_phy = nullptr;
    }

    xgl_config_t config;
    xgl_handle_t handle = nullptr;
    xgl_phy_ops_t phy_ops;
    xgl_route_item_t route;
    SendTestPhy* mock_phy = nullptr;
};

/*---------------------------------------------------------------------------*/
/* Parameter Validation Tests                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test send with NULL handle
 */
TEST_F(XglSendTest, SendWithNullHandle) {
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = (const uint8_t*)"test";
    tx_data.data_len = 4;
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;

    xgl_error_t err = xgl_send_at(nullptr, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test send with NULL tx_data
 */
TEST_F(XglSendTest, SendWithNullTxData) {
    xgl_error_t err = xgl_send_at(handle, nullptr, 0U);

    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test send with NULL data pointer
 */
TEST_F(XglSendTest, SendWithNullDataPointer) {
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = nullptr;
    tx_data.data_len = 4;
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;

    xgl_error_t err = xgl_send_at(handle, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test send with zero data length
 */
TEST_F(XglSendTest, SendWithZeroDataLength) {
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = (const uint8_t*)"test";
    tx_data.data_len = 0;
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;

    xgl_error_t err = xgl_send_at(handle, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

/**
 * \brief           Test send with invalid priority
 */
TEST_F(XglSendTest, SendWithInvalidPriority) {
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = (const uint8_t*)"test";
    tx_data.data_len = 4;
    tx_data.reliable = false;
    tx_data.priority = 8;
    /* Invalid: must be 0-7 */
    tx_data.timeout_ms = 0;

    xgl_error_t err = xgl_send_at(handle, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

/*---------------------------------------------------------------------------*/
/* Basic Send Tests                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test basic send with valid parameters
 */
TEST_F(XglSendTest, BasicSendSuccess) {
    const uint8_t test_data[] = "Hello, World!";
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = test_data;
    tx_data.data_len = sizeof(test_data) - 1;
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;

    /* Expect PHY TX to be called */
    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_))
        .Times(1)
        .WillOnce([](const uint8_t* bytes, size_t len, void*) {
            if (len < XGL_FRAME_HEADER_SIZE) {
                return XGL_ERR_INVALID_FRAME;
            }
            EXPECT_EQ(bytes[2], 1U);
            return XGL_OK;
        });

    xgl_error_t err = xgl_send_at(handle, &tx_data, 0U);

    EXPECT_EQ(err, XGL_OK);
}

TEST_F(XglSendTest, OrdinaryRejectsPayloadAboveRouteMtu) {
    ASSERT_LT(route.max_frame_size, config.protocol.max_frame_size);
    const size_t capacity = route.max_frame_size -
                            XGL_TEST_ZERO_COPY_APP_HEADER_LEN - XGL_CRC16_SIZE;
    std::vector<uint8_t> data(capacity + 1U, 0x5A);
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = data.data();
    tx_data.data_len = data.size();
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;

    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_EQ(xgl_send_at(handle, &tx_data, 0U), XGL_ERR_BUFFER_TOO_SMALL);
    tx_data.reliable = true;
    EXPECT_EQ(xgl_send_at(handle, &tx_data, 0U), XGL_ERR_BUFFER_TOO_SMALL);
#if XGL_FEATURE_ADVANCED_RUNTIME
    xgl_resource_snapshot_t resources{};
    ASSERT_EQ(xgl_resources_get(handle, &resources), XGL_OK);
    EXPECT_EQ(resources.peer_count, 0U);
#endif
    ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(mock_phy));
    tx_data.reliable = false;
    tx_data.data_len = capacity;
    EXPECT_CALL(*mock_phy, tx(testing::_, route.max_frame_size, testing::_))
        .WillOnce(testing::Return(XGL_OK));
    EXPECT_EQ(xgl_send_at(handle, &tx_data, 0U), XGL_OK);
}

TEST_F(XglSendTest, ScopedAuthenticatedCopyAndZeroCopyHonorExactRouteMtu) {
    xgl_destroy(handle);
    handle = nullptr;
    constexpr size_t mtu = 80U;
    constexpr size_t payload_offset = 24U + 3U + 14U + 15U;
    constexpr size_t tag_length = 8U;
    constexpr size_t capacity = mtu - payload_offset - tag_length - 2U;
    route.max_frame_size = mtu;
    xgl_auth_provider_t provider = {};
    provider.sign = send_test_auth_sign;
    provider.verify = send_test_auth_verify;
    provider.tag_len = tag_length;
    config.auth_provider = &provider;
    config.auth_required = true;
    handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);
    constexpr uint32_t connection = 17U;
    constexpr uint32_t epoch = 42U;
    const auto session = test_session_config(2U, connection, epoch);
    ASSERT_EQ(xgl_install_security_session(handle, &session), XGL_OK);

    uint8_t frame[mtu + 1U] = {};
    std::memset(frame + payload_offset, 0x5A, capacity + 1U);
    xgl_tx_data_t copy = {};
    copy.target_id = 2U;
    copy.connection_id = connection;
    copy.session_epoch = epoch;
    copy.data_type = 7U;
    copy.data = frame + payload_offset;
    copy.data_len = capacity + 1U;
    xgl_tx_zerocopy_request_t zero_copy = {};
    zero_copy.connection_id = connection;
    zero_copy.session_epoch = epoch;
    zero_copy.target_id = copy.target_id;
    zero_copy.data_type = copy.data_type;
    zero_copy.buffer = frame;
    zero_copy.buffer_size = sizeof(frame);
    zero_copy.data_offset = payload_offset;
    zero_copy.data_len = copy.data_len;
    const std::vector<uint8_t> original(frame, frame + sizeof(frame));

    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_EQ(xgl_send_at(handle, &copy, 0U), XGL_ERR_BUFFER_TOO_SMALL);
    xgl_tx_frame_layout_t layout = {1U, 1U};
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &zero_copy, &layout),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(layout.payload_offset, 0U);
    EXPECT_EQ(layout.frame_size, 0U);
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &zero_copy, 0U),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(std::memcmp(frame, original.data(), sizeof(frame)), 0);
    ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(mock_phy));

    copy.data_len = capacity;
    zero_copy.data_len = capacity;
    ASSERT_EQ(xgl_send_zerocopy_layout(handle, &zero_copy, &layout), XGL_OK);
    EXPECT_EQ(layout.payload_offset, payload_offset);
    EXPECT_EQ(layout.frame_size, mtu);
    uint64_t next_sequence = 0U;
    auto check_frame = [=, &next_sequence](const uint8_t* bytes, size_t len,
                                           void*) {
        xgl_wire_frame_view_t view = {};
        const auto decoded = xgl_wire_decode_frame(&view, bytes, len, nullptr);
        EXPECT_EQ(decoded, XGL_OK);
        if (decoded == XGL_OK) {
            EXPECT_EQ(view.header.connection_id, connection);
            EXPECT_EQ(view.session_epoch, epoch);
            EXPECT_EQ(view.payload_len, capacity);
            EXPECT_EQ(view.header.header_len, payload_offset);
            EXPECT_EQ(view.data_type, 7U);
            EXPECT_TRUE(view.authenticated);
            EXPECT_EQ(view.auth_tag_len, tag_length);
            EXPECT_EQ(view.nonce_id, next_sequence++);
            EXPECT_EQ(view.payload[0], 0x5AU);
        }
        return decoded;
    };
    testing::InSequence sequence;
    EXPECT_CALL(*mock_phy, tx(testing::_, mtu, testing::_))
        .WillOnce(check_frame);
    EXPECT_CALL(*mock_phy, tx(frame, mtu, testing::_)).WillOnce(check_frame);
    EXPECT_EQ(xgl_send_at(handle, &copy, 1U), XGL_OK);
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &zero_copy, 2U), XGL_OK);
}

/**
 * \brief           Test send to non-existent route
 */
TEST_F(XglSendTest, SendToNonExistentRoute) {
    const uint8_t test_data[] = "test";
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 99;
    /* No route for this ID */
    tx_data.data_type = 1;
    tx_data.data = test_data;
    tx_data.data_len = sizeof(test_data) - 1;
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;

    xgl_error_t err = xgl_send_at(handle, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
}

/*---------------------------------------------------------------------------*/
/* Zero-Copy Send Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test zero-copy send with NULL handle
 */
TEST_F(XglSendTest, ZeroCopySendWithNullHandle) {
    uint8_t buffer[128];
    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = XGL_FRAME_HEADER_SIZE;
    tx_data.data_len = 10;
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    xgl_error_t err = xgl_send_zerocopy_scoped_at(nullptr, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test zero-copy send with NULL tx_data
 */
TEST_F(XglSendTest, ZeroCopySendWithNullTxData) {
    xgl_error_t err = xgl_send_zerocopy_scoped_at(handle, nullptr, 0U);

    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test zero-copy send with invalid data offset
 */
TEST_F(XglSendTest, ZeroCopySendWithInvalidDataOffset) {
    uint8_t buffer[128];
    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = 10;
    /* Invalid: must be XGL_FRAME_HEADER_SIZE */
    tx_data.data_len = 10;
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    xgl_error_t err = xgl_send_zerocopy_scoped_at(handle, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

/**
 * \brief           Test zero-copy send with buffer too small
 */
TEST_F(XglSendTest, ZeroCopySendWithBufferTooSmall) {
    uint8_t buffer[20];
    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = XGL_FRAME_HEADER_SIZE;
    tx_data.data_len = 100;
    /* Too large for buffer */
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    xgl_error_t err = xgl_send_zerocopy_scoped_at(handle, &tx_data, 0U);

    EXPECT_EQ(err, XGL_ERR_BUFFER_TOO_SMALL);
}

TEST_F(XglSendTest, ZeroCopyUnreliableUsesCallerFrameBuffer) {
    uint8_t buffer[64] = {};
    const char payload[] = "zcopy";
    memcpy(buffer + XGL_TEST_ZERO_COPY_APP_HEADER_LEN, payload,
           sizeof(payload) - 1U);

    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = XGL_TEST_ZERO_COPY_APP_HEADER_LEN;
    tx_data.data_len = sizeof(payload) - 1U;
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    EXPECT_CALL(*mock_phy, tx(buffer,
                              XGL_TEST_ZERO_COPY_APP_HEADER_LEN +
                                  tx_data.data_len + XGL_CRC16_SIZE,
                              testing::_))
        .Times(1)
        .WillOnce(testing::Return(XGL_OK));

    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &tx_data, 0U), XGL_OK);
    EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1);
    EXPECT_EQ(std::memcmp(buffer + XGL_TEST_ZERO_COPY_APP_HEADER_LEN, payload,
                          sizeof(payload) - 1U),
              0);
}

TEST_F(XglSendTest, ZeroCopyBypassStillUpdatesLayerStats) {
    uint8_t buffer[64] = {};
    const char payload[] = "stats";
    memcpy(buffer + XGL_TEST_ZERO_COPY_APP_HEADER_LEN, payload,
           sizeof(payload) - 1U);

    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = XGL_TEST_ZERO_COPY_APP_HEADER_LEN;
    tx_data.data_len = sizeof(payload) - 1U;
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    const size_t expected_frame_len =
        XGL_TEST_ZERO_COPY_APP_HEADER_LEN + tx_data.data_len + XGL_CRC16_SIZE;
    EXPECT_CALL(*mock_phy, tx(buffer, expected_frame_len, testing::_))
        .Times(1)
        .WillOnce(testing::Return(XGL_OK));

    ASSERT_EQ(xgl_send_zerocopy_scoped_at(handle, &tx_data, 0U), XGL_OK);

    xgl_statistics_t stats = {};
    ASSERT_EQ(xgl_stats_get(handle, &stats), XGL_OK);
    EXPECT_EQ(stats.transport.tx_packets, 1U);
    EXPECT_EQ(stats.transport.tx_bytes, tx_data.data_len);
    EXPECT_EQ(stats.network.tx_packets, 1U);
    EXPECT_EQ(stats.network.tx_bytes, tx_data.data_len);
    EXPECT_EQ(stats.datalink.tx_packets, 1U);
    EXPECT_EQ(stats.datalink.tx_bytes, expected_frame_len);
}

TEST_F(XglSendTest, ZeroCopyAuthenticatesFrameWhenAuthenticationRequired) {
    xgl_destroy(handle);
    handle = nullptr;

    xgl_auth_provider_t provider = {};
    provider.sign = send_test_auth_sign;
    provider.verify = send_test_auth_verify;
    provider.tag_len = 8;
    provider.user_data = nullptr;
    config.auth_required = true;
    config.auth_provider = &provider;

    handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);
    auto trusted = test_session_config(2U);
    ASSERT_EQ(xgl_install_security_session(handle, &trusted), XGL_OK);

    constexpr size_t auth_header_len = XGL_WIRE_BASE_HEADER_SIZE +
                                       XGL_WIRE_EXT_HEADER_SIZE + 1U +
                                       XGL_WIRE_EXT_HEADER_SIZE + 13U;
    uint8_t buffer[128] = {};
    const char payload[] = "auth-zcopy";
    memcpy(buffer + auth_header_len, payload, sizeof(payload) - 1U);

    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = auth_header_len;
    tx_data.data_len = sizeof(payload) - 1U;
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    const size_t expected_frame_len =
        auth_header_len + tx_data.data_len + 8U + XGL_CRC16_SIZE;
    EXPECT_CALL(*mock_phy, tx(buffer, expected_frame_len, testing::_))
        .Times(1)
        .WillOnce(testing::Return(XGL_OK));

    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &tx_data, 0U), XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, buffer, expected_frame_len),
              XGL_OK);
    EXPECT_EQ(header.header_len, auth_header_len);
    EXPECT_EQ(header.payload_len, tx_data.data_len);
    EXPECT_NE(header.flags & XGL_WIRE_FLAG_AUTHENTICATED, 0);
    EXPECT_NE(header.flags & XGL_WIRE_FLAG_HAS_EXTENSIONS, 0);

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(
        xgl_wire_ext_cursor_init(&cursor, buffer + XGL_WIRE_BASE_HEADER_SIZE,
                                 header.header_len - XGL_WIRE_BASE_HEADER_SIZE),
        XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_DATA_TYPE);
    ASSERT_EQ(ext.len, 1U);
    EXPECT_EQ(ext.value[0], tx_data.data_type);
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_SECURITY);

    uint32_t key_id = 0;
    uint64_t nonce_id = 0;
    uint8_t tag_len = 0;
    ASSERT_EQ(xgl_wire_decode_security_ext_value(ext.value, ext.len, &key_id,
                                                 &nonce_id, &tag_len),
              XGL_OK);
    EXPECT_EQ(key_id, 7U);
    EXPECT_EQ(tag_len, 8U);

    bool valid = false;
    ASSERT_EQ(test_verify_trusted_frame(
                  buffer, expected_frame_len - XGL_CRC16_SIZE,
                  header.header_len, header.payload_len, 7, &provider, &valid),
              XGL_OK);
    EXPECT_TRUE(valid);
}

TEST_F(XglSendTest, ZeroCopyUnreliableRejectsPayloadExceedingRouteMtu) {
    xgl_destroy(handle);
    handle = nullptr;
    route.max_frame_size = 64;
    handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);

    uint8_t buffer[128] = {};
    memset(buffer + XGL_TEST_ZERO_COPY_APP_HEADER_LEN, 0xAB, 60);
    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = XGL_TEST_ZERO_COPY_APP_HEADER_LEN;
    tx_data.data_len = 60;
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &tx_data, 0U),
              XGL_ERR_BUFFER_TOO_SMALL);
}

TEST_F(XglSendTest, ZeroCopyUnreliableWritesDefaultTtlAndEmptySession) {
    uint8_t buffer[64] = {};
    const char payload[] = "zcopy";
    memcpy(buffer + XGL_TEST_ZERO_COPY_APP_HEADER_LEN, payload,
           sizeof(payload) - 1U);

    xgl_tx_zerocopy_request_t tx_data = {};
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = XGL_TEST_ZERO_COPY_APP_HEADER_LEN;
    tx_data.data_len = sizeof(payload) - 1U;
    tx_data.target_id = 2;
    tx_data.data_type = 1;

    tx_data.priority = 0;

    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_))
        .Times(1)
        .WillOnce(testing::Return(XGL_OK));

    ASSERT_EQ(xgl_send_zerocopy_scoped_at(handle, &tx_data, 0U), XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, buffer, sizeof(buffer)), XGL_OK);
    EXPECT_EQ(header.ttl, XGL_DEFAULT_TTL);
    EXPECT_EQ(header.connection_id, 0U);
}

namespace {
struct StaticAuthEndpoint {
    StaticAuthEndpoint* remote = nullptr;
    uint8_t incoming[2048] = {};
    size_t incoming_length = 0;
    uint64_t sequences[32] = {};
    size_t sign_calls = 0;
    unsigned deliveries = 0;
    unsigned dropped_acks = 0;
    bool drop_first_ack = false;
    uint32_t first_data_number = 0;
    unsigned data_frames = 0;
};

/** \brief           Record independent security sequences in bounded test
 * storage. */
static xgl_error_t static_auth_sign(const xgl_auth_input_t* input, uint8_t* tag,
                                    size_t capacity, size_t* length,
                                    void* context) {
    auto* endpoint = static_cast<StaticAuthEndpoint*>(context);
    if (endpoint->sign_calls >= 32U) {
        return XGL_ERR_NO_MEMORY;
    }
    endpoint->sequences[endpoint->sign_calls++] = input->security_seq;
    return send_test_auth_sign(input, tag, capacity, length, nullptr);
}

/** \brief           Synchronously copy into the peer while dropping exactly one
 * ACK. */
static xgl_error_t static_auth_tx(const uint8_t* bytes, size_t length,
                                  void* context) {
    auto* endpoint = static_cast<StaticAuthEndpoint*>(context);
    xgl_wire_frame_view_t view = {};
    if (xgl_wire_decode_frame(&view, bytes, length, nullptr) != XGL_OK) {
        return XGL_ERR_INVALID_FRAME;
    }
    if (view.header.packet_type == XGL_PACKET_TYPE_DATA) {
        if (endpoint->data_frames++ == 0U) {
            endpoint->first_data_number = view.header.packet_number;
        } else {
            EXPECT_EQ(view.header.packet_number, endpoint->first_data_number);
        }
    }
    if (endpoint->drop_first_ack &&
        view.header.packet_type == XGL_PACKET_TYPE_ACK) {
        endpoint->drop_first_ack = false;
        endpoint->dropped_acks++;
        return XGL_OK;
    }
    auto* remote = endpoint->remote;
    if (length > sizeof(remote->incoming) - remote->incoming_length) {
        return XGL_ERR_BUSY;
    }
    std::memcpy(remote->incoming + remote->incoming_length, bytes, length);
    remote->incoming_length += length;
    return XGL_OK;
}

/** \brief           Preserve bytes outside the requested RX chunk. */
static xgl_error_t static_auth_rx(uint8_t* bytes, size_t* length,
                                  void* context) {
    auto* endpoint = static_cast<StaticAuthEndpoint*>(context);
    if (*length > endpoint->incoming_length) {
        *length = endpoint->incoming_length;
    }
    std::memcpy(bytes, endpoint->incoming, *length);
    endpoint->incoming_length -= *length;
    std::memmove(endpoint->incoming, endpoint->incoming + *length,
                 endpoint->incoming_length);
    return XGL_OK;
}

/** \brief           Count application deliveries without reentering the
 * protocol. */
static xgl_error_t static_auth_accept(xgl_handle_t,
                                      const xgl_rx_info_t* /*info*/,
                                      const uint8_t* bytes, size_t length,
                                      void* context) {
    if (length != 3U || std::memcmp(bytes, "abc", 3U) != 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    static_cast<StaticAuthEndpoint*>(context)->deliveries++;
    return XGL_OK;
}
} /* namespace */

TEST(XglStaticAuthenticatedSendTest,
     LostAckRetransmitsWithFreshNonceAndExactlyOnceDelivery) {
    alignas(xgl_max_align_t) static uint8_t storage_a[32768];
    alignas(xgl_max_align_t) static uint8_t storage_b[32768];
    StaticAuthEndpoint a, b;
    a.remote = &b;
    b.remote = &a;
    b.drop_first_ack = true;
    xgl_phy_ops_t phy_a = {static_auth_tx, static_auth_rx, &a};
    xgl_phy_ops_t phy_b = {static_auth_tx, static_auth_rx, &b};
    xgl_route_item_t route_a = {2U, &phy_a, 128U, 1000U};
    xgl_route_item_t route_b = {1U, &phy_b, 128U, 1000U};
    xgl_auth_provider_t provider_a = {static_auth_sign, send_test_auth_verify,
                                      8U, &a};
    xgl_auth_provider_t provider_b = {static_auth_sign, send_test_auth_verify,
                                      8U, &b};
    xgl_config_t config_a = xgl_test_two_packet_config();
    config_a.protocol.window_size = 1U;
    config_a.protocol.ack_timeout_ms = 100U;
    config_a.route_table = &route_a;
    config_a.route_table_len = 1U;
    config_a.auth_required = true;
    config_a.auth_provider = &provider_a;
    config_a.rx_callback = static_auth_accept;
    config_a.rx_user_data = &a;
    xgl_config_t config_b = config_a;
    config_b.source_id = 2U;
    config_b.route_table = &route_b;
    config_b.auth_provider = &provider_b;
    config_b.rx_user_data = &b;
    xgl_handle_t handle_a = nullptr, handle_b = nullptr;
    ASSERT_EQ(
        xgl_init_static(&config_a, storage_a, sizeof(storage_a), &handle_a),
        XGL_OK);
    ASSERT_EQ(
        xgl_init_static(&config_b, storage_b, sizeof(storage_b), &handle_b),
        XGL_OK);
    auto trusted_a = test_session_config(2U, 17U, 42U);
    auto trusted_b = test_session_config(1U, 17U, 42U, 7U, true);
    ASSERT_EQ(xgl_install_security_session(handle_a, &trusted_a), XGL_OK);
    ASSERT_EQ(xgl_install_security_session(handle_b, &trusted_b), XGL_OK);
    xgl_tx_data_t tx = {};
    tx.target_id = 2U;
    tx.data = reinterpret_cast<const uint8_t*>("abc");
    tx.data_len = 3U;
    tx.reliable = true;
    tx.timeout_ms = 100U;
    tx.connection_id = 17U;
    tx.session_epoch = 42U;
    ASSERT_EQ(xgl_send_at(handle_a, &tx, 0U), XGL_OK);
    const xgl_work_budget_t budget = {256U, 1000U};
    for (uint32_t now = 0; now < 500U; ++now) {
        ASSERT_EQ(xgl_step(handle_a, now, &budget), XGL_OK);
        ASSERT_EQ(xgl_step(handle_b, now, &budget), XGL_OK);
    }
    EXPECT_EQ(b.deliveries, 1U);
    EXPECT_EQ(b.dropped_acks, 1U);
    EXPECT_EQ(a.data_frames, 2U);
    tx.target_id = 1U;
    ASSERT_EQ(xgl_send_at(handle_b, &tx, 500U), XGL_OK);
    for (uint32_t now = 500; now < 700U; ++now) {
        ASSERT_EQ(xgl_step(handle_a, now, &budget), XGL_OK);
        ASSERT_EQ(xgl_step(handle_b, now, &budget), XGL_OK);
    }
    EXPECT_EQ(a.deliveries, 1U);
    for (const auto* endpoint : {&a, &b}) {
        ASSERT_GE(endpoint->sign_calls, 2U);
        for (size_t i = 1; i < endpoint->sign_calls; ++i) {
            EXPECT_GT(endpoint->sequences[i], endpoint->sequences[i - 1U]);
        }
    }
    xgl_destroy(handle_a);
    xgl_destroy(handle_b);
}

TEST_F(XglSendTest, ZeroCopyRejectsFrameAboveInstanceMtuEvenWhenRouteAllowsIt) {
    xgl_destroy(handle);
    handle = nullptr;
    config.protocol.max_frame_size = 128U;
    route.max_frame_size = 256U;
    handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);
    uint8_t buffer[256] = {};
    xgl_tx_zerocopy_request_t tx = {};
    tx.buffer = buffer;
    tx.buffer_size = sizeof(buffer);
    tx.data_offset = XGL_WIRE_BASE_HEADER_SIZE;
    tx.data_len = 160U;
    tx.target_id = 2U;
    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &tx, 0U),
              XGL_ERR_BUFFER_TOO_SMALL);
}
