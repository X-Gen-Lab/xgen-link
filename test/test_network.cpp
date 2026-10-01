#include <xgen/memory/libc_allocator.h>
/**
 * \file            test_network.cpp
 * \brief           Unit tests for network layer
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_config.h>

#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

/*---------------------------------------------------------------------------*/
/* Test PHY Operations                                                       */
/*---------------------------------------------------------------------------*/

static xgl_error_t test_phy_tx(const uint8_t* data, size_t len,
                               void* user_data) {
    (void)data;
    (void)len;
    int* count = (int*)user_data;
    if (count) {
        (*count)++;
    }
    return XGL_OK;
}

static xgl_error_t test_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    (void)buffer;
    (void)user_data;
    *len = 0;
    return XGL_OK;
}

struct CaptureTx {
    int count = 0;
    std::vector<uint8_t> bytes;
};

static xgl_error_t capture_phy_tx(const uint8_t* data, size_t len,
                                  void* user_data) {
    auto* capture = static_cast<CaptureTx*>(user_data);
    if (capture != nullptr) {
        capture->count++;
        capture->bytes.assign(data, data + len);
    }
    return XGL_OK;
}

struct NetworkForwardAllocatorState {
    size_t alloc_count = 0;
    size_t free_count = 0;
};

static NetworkForwardAllocatorState* g_network_forward_allocator_state =
    nullptr;

static void* network_forward_test_malloc(void*, size_t size) {
    if (g_network_forward_allocator_state != nullptr) {
        g_network_forward_allocator_state->alloc_count++;
    }
    return std::malloc(size);
}

static void network_forward_test_free(void*, void* ptr) {
    if (g_network_forward_allocator_state != nullptr) {
        g_network_forward_allocator_state->free_count++;
    }
    std::free(ptr);
}

static xgl_error_t network_test_auth_sign(const xgl_auth_input_t* input,
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

    uint32_t acc = key_id ^ 0xA5A5A5A5U;
    for (size_t i = 0; i < aad_len; ++i) {
        acc = (acc * 31U) ^ aad[i];
    }
    for (size_t i = 0; i < payload_len; ++i) {
        acc = (acc * 31U) ^ payload[i];
    }
    for (size_t i = 0; i < 8U; ++i) {
        tag[i] = static_cast<uint8_t>((acc >> ((i % 4U) * 8U)) & 0xFFU);
    }
    *tag_len = 8U;
    return XGL_OK;
}

static xgl_error_t network_test_auth_verify(const xgl_auth_input_t* input,
                                            const uint8_t* tag, size_t tag_len,
                                            bool* valid, void* user_data) {
    if (tag == nullptr || valid == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    uint8_t expected[8] = {};
    size_t expected_len = 0;
    xgl_error_t err = network_test_auth_sign(input, expected, sizeof(expected),
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

class XglNetworkTest : public ::testing::Test {
  protected:
    void SetUp() override {
        /* Initialize route table */
        xgl_route_table_init(&route_table, 4, xgm_allocator_libc());

        /* Initialize statistics */
        memset(&stats, 0, sizeof(stats));

        /* Initialize network context */
        xgl_network_config_t config = {};
        config.local_id = LOCAL_ID;
        config.route_table = &route_table;
        config.upper_layer = nullptr;
        config.lower_layer = nullptr;
        config.error_callback = nullptr;
        config.callback_user_data = nullptr;
        config.stats = &stats;
        config.allocator = xgm_allocator_libc();
        xgl_network_init(&network_ctx, &config);

        /* Initialize PHY operations */
        phy_tx_count = 0;
        phy_ops.tx = test_phy_tx;
        phy_ops.rx = test_phy_rx;
        phy_ops.user_data = &phy_tx_count;

        xgl_datalink_config_t datalink_config = {};
        datalink_config.rx_cache = datalink_cache;
        datalink_config.rx_cache_size = sizeof(datalink_cache);
        datalink_config.stats = &datalink_stats;
        datalink_config.allocator = xgm_allocator_libc();
        ASSERT_EQ(xgl_datalink_init(&datalink_ctx, &datalink_config), XGL_OK);
        ASSERT_EQ(xgl_datalink_get_interface(&datalink_ctx, &datalink_iface),
                  XGL_OK);
        network_ctx.lower_layer = &datalink_iface;
    }

    void TearDown() override {
        xgl_route_table_destroy(&route_table);
    }

    static constexpr uint8_t LOCAL_ID = 1;
    static constexpr uint8_t REMOTE_ID = 2;
    static constexpr uint8_t FORWARD_ID = 3;

    std::vector<uint8_t> make_frame(uint16_t source_id, uint16_t target_id,
                                    uint8_t ttl = XGL_DEFAULT_TTL,
                                    const char* payload = "hello") {
        xgl_frame_t frame = {};
        xgl_frame_params_t params = {};
        params.source_id = source_id;
        params.target_id = target_id;
        params.data_type = 1;
        params.payload = reinterpret_cast<const uint8_t*>(payload);
        params.payload_len = std::strlen(payload);
        params.reliable = true;
        params.reliability_class = XGL_RELIABILITY_NONE;
        params.fragment = false;
        params.priority = 0;
        params.ttl = ttl;
        EXPECT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

        std::vector<uint8_t> bytes(
            xgl_frame_calculate_size(params.payload_len));
        size_t written = 0;
        EXPECT_EQ(
            xgl_frame_serialize(bytes.data(), bytes.size(), &frame, &written),
            XGL_OK);
        bytes.resize(written);
        return bytes;
    }

    xgl_route_table_t route_table;
    xgl_network_ctx_t network_ctx;
    xgl_layer_stats_t stats;
    uint8_t datalink_cache[256] = {};
    xgl_layer_stats_t datalink_stats = {};
    xgl_datalink_ctx_t datalink_ctx = {};
    xgl_frame_interface_t datalink_iface = {};
    xgl_phy_ops_t phy_ops;
    int phy_tx_count;
};

TEST_F(XglNetworkTest, SendPreservesEpochForDataAckAndControl) {
    ASSERT_EQ(
        xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1),
        XGL_OK);
    std::vector<uint8_t> captured(256);
    xgl_frame_interface_t lower = {};
    lower.ctx = &captured;
    lower.send = [](void* ctx, xgl_handle_t,
                    const xgl_frame_tx_message_t* data) {
        auto* bytes = static_cast<std::vector<uint8_t>*>(ctx);
        auto* message = static_cast<const xgl_frame_tx_message_t*>(data);
        bytes->resize(256);
        size_t written = 0;
        auto err = xgl_frame_serialize(bytes->data(), bytes->size(),
                                       message->frame, &written);
        bytes->resize(written);
        return err;
    };
    network_ctx.lower_layer = &lower;
    const uint8_t payload[] = {1};
    xgl_packet_data_t data = {};
    data.data_len = sizeof(payload);
    data.data = payload;
    for (uint8_t type :
         {XGL_PACKET_TYPE_DATA, XGL_PACKET_TYPE_ACK, XGL_PACKET_TYPE_CONTROL}) {
        xgl_packet_t packet = {};
        packet.source_id = LOCAL_ID;
        packet.target_id = REMOTE_ID;
        packet.connection_id = 0x1234000BU;
        packet.session_epoch = 100;
        packet.packet_type = type;
        packet.data = &data;
        ASSERT_EQ(xgl_network_send(&network_ctx, &packet), XGL_OK);
        xgl_wire_frame_view_t metadata = {};
        ASSERT_EQ(xgl_wire_decode_frame(&metadata, captured.data(),
                                        captured.size(), nullptr),
                  XGL_OK);
        EXPECT_EQ(metadata.header.connection_id, packet.connection_id);
        EXPECT_EQ(metadata.session_epoch, 100U);
        EXPECT_EQ(metadata.header.packet_type, type);
    }
}

TEST_F(XglNetworkTest, DefaultConnectionDoesNotUseLocalShortSession) {
    ASSERT_EQ(
        xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1),
        XGL_OK);
    uint32_t connection_id = UINT32_MAX;
    xgl_frame_interface_t lower = {};
    lower.ctx = &connection_id;
    lower.send = [](void* ctx, xgl_handle_t,
                    const xgl_frame_tx_message_t* data) {
        *static_cast<uint32_t*>(ctx) =
            static_cast<const xgl_frame_tx_message_t*>(data)
                ->frame->header.connection_id;
        return XGL_OK;
    };
    network_ctx.lower_layer = &lower;
    const uint8_t payload[] = {1};
    xgl_packet_data_t data = {};
    data.data_len = sizeof(payload);
    data.data = payload;
    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = REMOTE_ID;
    packet.data = &data;
    ASSERT_EQ(xgl_network_send(&network_ctx, &packet), XGL_OK);
    EXPECT_EQ(connection_id, 0U);
}

TEST_F(XglNetworkTest, EpochExtensionCountsAgainstRouteMtu) {
    const uint16_t mtu = XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE + 1;
    ASSERT_EQ(
        xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, mtu, 100, 1),
        XGL_OK);
    unsigned sends = 0;
    xgl_frame_interface_t lower = {};
    lower.ctx = &sends;
    lower.send = [](void* ctx, xgl_handle_t, const xgl_frame_tx_message_t*) {
        ++*static_cast<unsigned*>(ctx);
        return XGL_OK;
    };
    network_ctx.lower_layer = &lower;
    const uint8_t payload[] = {1};
    xgl_packet_data_t data = {};
    data.data_len = sizeof(payload);
    data.data = payload;
    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = REMOTE_ID;
    packet.session_epoch = 100;
    packet.data = &data;
    EXPECT_EQ(xgl_network_send(&network_ctx, &packet),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(sends, 0U);
}

TEST_F(XglNetworkTest, FrameMetadataRejectsCorruptPayloadAndNullExtensionSpan) {
    auto bytes = make_frame(REMOTE_ID, LOCAL_ID);
    bytes[XGL_WIRE_BASE_HEADER_SIZE] ^= 0x80;
    xgl_wire_frame_view_t frame = {};
    EXPECT_NE(
        xgl_wire_decode_frame(&frame, bytes.data(), bytes.size(), nullptr),
        XGL_OK);
    xgl_wire_ext_metadata_t extensions = {};
    EXPECT_EQ(xgl_wire_decode_ext_metadata(nullptr, 3, &extensions),
              XGL_ERR_NULL_POINTER);
}

TEST_F(XglNetworkTest, FrameMetadataRejectsEveryTruncatedHeaderExtension) {
    uint8_t bytes[64] = {};
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE + XGL_SESSION_EXT_SIZE;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = REMOTE_ID;
    header.target_id = LOCAL_ID;
    ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header), XGL_OK);
    uint8_t value[XGL_SESSION_EXT_VALUE_SIZE] = {};
    size_t written = 0;
    ASSERT_EQ(
        xgl_wire_encode_session_ext_value(value, sizeof(value), 9, 0, &written),
        XGL_OK);
    ASSERT_EQ(xgl_wire_encode_ext(bytes + XGL_WIRE_BASE_HEADER_SIZE,
                                  sizeof(bytes) - XGL_WIRE_BASE_HEADER_SIZE,
                                  XGL_WIRE_EXT_SESSION, value, written,
                                  &written),
              XGL_OK);
    for (size_t size = XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE;
         size < header.header_len; ++size) {
        xgl_wire_frame_view_t metadata = {};
        EXPECT_EQ(xgl_wire_decode_frame(&metadata, bytes, size, nullptr),
                  XGL_ERR_INVALID_FRAME);
    }
}

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, InitializeNetworkContext) {
    xgl_network_ctx_t ctx;
    xgl_route_table_t table;
    xgl_layer_stats_t test_stats = {0};

    xgl_route_table_init(&table, 4, xgm_allocator_libc());

    xgl_network_config_t config = {};
    config.local_id = 1;
    config.route_table = &table;
    config.upper_layer = nullptr;
    config.lower_layer = nullptr;
    config.error_callback = nullptr;
    config.callback_user_data = nullptr;
    config.stats = &test_stats;
    config.allocator = xgm_allocator_libc();
    xgl_error_t err = xgl_network_init(&ctx, &config);

    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(ctx.local_id, 1);
    EXPECT_EQ(ctx.route_table, &table);

    xgl_route_table_destroy(&table);
}

/*---------------------------------------------------------------------------*/
/* Route Table Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, AddRoute) {
    xgl_error_t err =
        xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);
    EXPECT_EQ(err, XGL_OK);

    xgl_route_item_t* found = xgl_route_table_lookup(&route_table, REMOTE_ID);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->target_id, REMOTE_ID);
}

TEST_F(XglNetworkTest, RemoveRoute) {
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);

    xgl_error_t err = xgl_route_table_remove(&route_table, REMOTE_ID);
    EXPECT_EQ(err, XGL_OK);

    xgl_route_item_t* found = xgl_route_table_lookup(&route_table, REMOTE_ID);
    EXPECT_EQ(found, nullptr);
}

TEST_F(XglNetworkTest, LookupNonexistentRoute) {
    xgl_route_item_t* found = xgl_route_table_lookup(&route_table, 99);
    EXPECT_EQ(found, nullptr);
}

/*---------------------------------------------------------------------------*/
/* Statistics Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, StatisticsInitiallyZero) {
    EXPECT_EQ(stats.tx_packets, 0);
    EXPECT_EQ(stats.tx_bytes, 0);
    EXPECT_EQ(stats.rx_packets, 0);
    EXPECT_EQ(stats.rx_bytes, 0);
}

/*---------------------------------------------------------------------------*/
/* Address Validation Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ValidateValidAddress) {
    bool valid =
        xgl_network_validate_address(&network_ctx, REMOTE_ID, LOCAL_ID);
    EXPECT_TRUE(valid);
}

TEST_F(XglNetworkTest, ValidateBroadcastSourceInvalid) {
    bool valid =
        xgl_network_validate_address(&network_ctx, LOCAL_ID, XGL_BROADCAST_ID);
    EXPECT_FALSE(valid);
}

TEST_F(XglNetworkTest, ValidateZeroSourceInvalid) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, 0);
    EXPECT_FALSE(valid);
}

TEST_F(XglNetworkTest, ValidateBroadcastTargetValid) {
    bool valid =
        xgl_network_validate_address(&network_ctx, XGL_BROADCAST_ID, LOCAL_ID);
    EXPECT_TRUE(valid);
}

TEST_F(XglNetworkTest, ValidateSelfAddressInvalid) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, LOCAL_ID);
    EXPECT_FALSE(valid);
}

/*---------------------------------------------------------------------------*/
/* Local Node Detection Tests                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, IsLocalForLocalId) {
    bool is_local = xgl_network_is_local(&network_ctx, LOCAL_ID);
    EXPECT_TRUE(is_local);
}

TEST_F(XglNetworkTest, IsLocalForBroadcast) {
    bool is_local = xgl_network_is_local(&network_ctx, XGL_BROADCAST_ID);
    EXPECT_TRUE(is_local);
}

TEST_F(XglNetworkTest, IsNotLocalForRemoteId) {
    bool is_local = xgl_network_is_local(&network_ctx, REMOTE_ID);
    EXPECT_FALSE(is_local);
}

/*---------------------------------------------------------------------------*/
/* Send Packet Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, SendPacketWithoutRoute) {
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = 10;
    packet_data.data = (uint8_t*)"test_data";

    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = REMOTE_ID;
    packet.data_type = 1;
    packet.reliable = 1;
    packet.priority = 0;
    packet.data = &packet_data;

    xgl_error_t err = xgl_network_send(&network_ctx, &packet);
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
}

TEST_F(XglNetworkTest, SendPacketWithRoute) {
    /* Add route */
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);

    xgl_packet_data_t packet_data = {};
    packet_data.data_len = 10;
    packet_data.data = (uint8_t*)"test_data";

    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = REMOTE_ID;
    packet.data_type = 1;
    packet.reliable = 1;
    packet.priority = 0;
    packet.data = &packet_data;

    /* Mock lower layer interface */
    xgl_frame_interface_t lower_layer;
    lower_layer.ctx = nullptr;
    lower_layer.send = [](void* ctx, xgl_handle_t handle,
                          const xgl_frame_tx_message_t* data) -> xgl_error_t {
        (void)ctx;
        (void)handle;
        (void)data;
        return XGL_OK;
    };
    lower_layer.receive = nullptr;

    network_ctx.lower_layer = &lower_layer;

    xgl_error_t err = xgl_network_send(&network_ctx, &packet);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
}

TEST_F(XglNetworkTest, SendPacketEncodesApplicationTypeAsHeaderExtension) {
    constexpr uint8_t kAppTypeThatCollidesWithAck = XGL_PACKET_TYPE_ACK;
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);

    const uint8_t payload[] = {'a', 'p', 'p'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;

    struct CapturedFrameMessage {
        xgl_frame_t frame = {};
        std::vector<uint8_t> extensions;
    } capture;

    xgl_frame_interface_t lower_layer = {};
    lower_layer.ctx = &capture;
    lower_layer.send = [](void* ctx, xgl_handle_t handle,
                          const xgl_frame_tx_message_t* data) -> xgl_error_t {
        (void)handle;
        auto* capture = static_cast<CapturedFrameMessage*>(ctx);
        auto* message = static_cast<const xgl_frame_tx_message_t*>(data);
        if (capture == nullptr || message == nullptr ||
            message->frame == nullptr) {
            return XGL_ERR_NULL_POINTER;
        }
        capture->frame = *message->frame;
        if (message->frame->extensions != nullptr &&
            message->frame->extensions_len > 0U) {
            capture->extensions.assign(message->frame->extensions,
                                       message->frame->extensions +
                                           message->frame->extensions_len);
            capture->frame.extensions = capture->extensions.data();
        }
        return XGL_OK;
    };
    network_ctx.lower_layer = &lower_layer;

    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = REMOTE_ID;
    packet.data_type = kAppTypeThatCollidesWithAck;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.priority = 4;
    packet.data = &packet_data;

    ASSERT_EQ(xgl_network_send(&network_ctx, &packet), XGL_OK);
    EXPECT_EQ(capture.frame.header.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_NE(capture.frame.header.flags & XGL_WIRE_FLAG_HAS_EXTENSIONS, 0U);
    ASSERT_NE(capture.frame.extensions, nullptr);

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor, capture.extensions.data(),
                                       capture.extensions.size()),
              XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_DATA_TYPE);
    ASSERT_EQ(ext.len, 1U);
    EXPECT_EQ(ext.value[0], kAppTypeThatCollidesWithAck);
}

TEST_F(XglNetworkTest, SendPacketRejectsConflictingDataTypeExtension) {
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);

    uint8_t ext_buf[XGL_DATA_TYPE_EXT_SIZE] = {};
    uint8_t ext_data_type = 7U;
    size_t ext_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(ext_buf, sizeof(ext_buf),
                                  XGL_WIRE_EXT_DATA_TYPE, &ext_data_type, 1U,
                                  &ext_len),
              XGL_OK);

    const uint8_t payload[] = {'a'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = REMOTE_ID;
    packet.data_type = 9U;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.data = &packet_data;
    packet.extensions = ext_buf;
    packet.extensions_len = ext_len;

    EXPECT_EQ(xgl_network_send(&network_ctx, &packet), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglNetworkTest, SendPacketRouteMtuIncludesAuthenticationOverhead) {
    xgl_auth_provider_t provider = {};
    provider.sign = network_test_auth_sign;
    provider.verify = network_test_auth_verify;
    provider.tag_len = 8;
    provider.user_data = nullptr;
    network_ctx.auth_required = true;
    network_ctx.auth_provider = &provider;
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 60, 100, 1);

    const uint8_t payload[20] = {};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = REMOTE_ID;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_NONE;
    packet.priority = 0;
    packet.data = &packet_data;

    bool lower_called = false;
    xgl_frame_interface_t lower_layer = {};
    lower_layer.ctx = &lower_called;
    lower_layer.send = [](void* ctx, xgl_handle_t handle,
                          const xgl_frame_tx_message_t* data) -> xgl_error_t {
        (void)handle;
        (void)data;
        *static_cast<bool*>(ctx) = true;
        return XGL_OK;
    };
    network_ctx.lower_layer = &lower_layer;

    EXPECT_EQ(xgl_network_send(&network_ctx, &packet),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_FALSE(lower_called);
}

TEST_F(XglNetworkTest, SendPacketNullPointer) {
    xgl_error_t err = xgl_network_send(nullptr, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Receive Packet Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ReceivePacketForLocalNode) {
    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, LOCAL_ID);

    /* Mock upper layer interface */
    bool upper_called = false;
    xgl_packet_interface_t upper_layer;
    upper_layer.ctx = &upper_called;
    upper_layer.receive = [](void* ctx, xgl_handle_t handle,
                             const xgl_packet_t* data) -> xgl_error_t {
        (void)handle;
        (void)data;
        bool* called = (bool*)ctx;
        *called = true;
        return XGL_OK;
    };
    upper_layer.send = nullptr;

    network_ctx.upper_layer = &upper_layer;

    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr,
                                          frame_buf.data(), frame_buf.size());
    EXPECT_EQ(err, XGL_OK);
    EXPECT_TRUE(upper_called);
    EXPECT_EQ(stats.rx_packets, 1);
}

TEST_F(XglNetworkTest, ReceivePacketForForwarding) {
    /* Add route for forwarding */
    xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, FORWARD_ID);

    int initial_tx_count = phy_tx_count;

    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr,
                                          frame_buf.data(), frame_buf.size());
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(phy_tx_count, initial_tx_count + 1);  // Should forward
}

TEST_F(XglNetworkTest, ForwardingUsesDatalinkSubmissionAndCountsWireBytes) {
    ASSERT_EQ(
        xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1),
        XGL_OK);
    uint8_t cache[256] = {};
    xgl_layer_stats_t datalink_stats = {};
    xgl_datalink_config_t config = {};
    config.rx_cache = cache;
    config.rx_cache_size = sizeof(cache);
    config.stats = &datalink_stats;
    xgl_datalink_ctx_t datalink = {};
    ASSERT_EQ(xgl_datalink_init(&datalink, &config), XGL_OK);
    xgl_frame_interface_t lower = {};
    ASSERT_EQ(xgl_datalink_get_interface(&datalink, &lower), XGL_OK);
    network_ctx.lower_layer = &lower;
    const auto frame = make_frame(REMOTE_ID, FORWARD_ID);

    ASSERT_EQ(
        xgl_network_receive(&network_ctx, nullptr, frame.data(), frame.size()),
        XGL_OK);
    EXPECT_EQ(phy_tx_count, 1);
    EXPECT_EQ(datalink_stats.tx_packets, 1U);
    EXPECT_EQ(datalink_stats.tx_bytes, frame.size());
}

TEST_F(XglNetworkTest, ForwardingRejectsMissingPhyTransmitOperation) {
    phy_ops.tx = nullptr;
    ASSERT_EQ(
        xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1),
        XGL_OK);
    const auto frame = make_frame(REMOTE_ID, FORWARD_ID);

    EXPECT_EQ(
        xgl_network_receive(&network_ctx, nullptr, frame.data(), frame.size()),
        XGL_ERR_TX_FAILED);
    EXPECT_EQ(phy_tx_count, 0);
}

TEST_F(XglNetworkTest, ForwardingUsesTargetRouteEgressPhy) {
    int first_phy_count = 0;
    int second_phy_count = 0;
    xgl_phy_ops_t first_phy = {};
    first_phy.tx = test_phy_tx;
    first_phy.rx = test_phy_rx;
    first_phy.user_data = &first_phy_count;
    xgl_phy_ops_t second_phy = {};
    second_phy.tx = test_phy_tx;
    second_phy.rx = test_phy_rx;
    second_phy.user_data = &second_phy_count;

    ASSERT_EQ(xgl_route_table_add(&route_table, 4, &first_phy, 256, 100, 10),
              XGL_OK);
    ASSERT_EQ(xgl_route_table_add(&route_table, 5, &second_phy, 256, 100, 1),
              XGL_OK);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, 5);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(),
                                  frame_buf.size()),
              XGL_OK);
    EXPECT_EQ(first_phy_count, 0);
    EXPECT_EQ(second_phy_count, 1);
    EXPECT_EQ(stats.tx_packets, 1);
}

TEST_F(XglNetworkTest, ForwardingDropsExpiredTtl) {
    ASSERT_EQ(
        xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1),
        XGL_OK);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, FORWARD_ID, 0);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(),
                                  frame_buf.size()),
              XGL_ERR_TTL_EXPIRED);
    EXPECT_EQ(phy_tx_count, 0);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ForwardingDropsPacketWithTtlOneBeforeForwarding) {
    ASSERT_EQ(
        xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1),
        XGL_OK);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, FORWARD_ID, 1);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(),
                                  frame_buf.size()),
              XGL_ERR_TTL_EXPIRED);
    EXPECT_EQ(phy_tx_count, 0);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ForwardingRejectsRouteMtuOverflow) {
    ASSERT_EQ(
        xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 32, 100, 1),
        XGL_OK);

    std::vector<uint8_t> frame_buf =
        make_frame(REMOTE_ID, FORWARD_ID, XGL_DEFAULT_TTL,
                   "payload-larger-than-route-mtu");
    ASSERT_GT(frame_buf.size(), 32U);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(),
                                  frame_buf.size()),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(phy_tx_count, 0);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ForwardingAllocatesWholeFrameFromNetworkResource) {
    NetworkForwardAllocatorState allocator_state;
    g_network_forward_allocator_state = &allocator_state;
    xgm_allocator_t allocator = {};
    allocator.ctx = nullptr;
    allocator.alloc = network_forward_test_malloc;
    allocator.free = network_forward_test_free;

    xgl_network_ctx_t ctx = {};
    xgl_layer_stats_t local_stats = {};
    xgl_network_config_t config = {};
    config.local_id = LOCAL_ID;
    config.route_table = &route_table;
    config.stats = &local_stats;
    config.allocator = &allocator;
    config.lower_layer = &datalink_iface;
    ASSERT_EQ(xgl_network_init(&ctx, &config), XGL_OK);

    CaptureTx capture;
    xgl_phy_ops_t capture_phy = {};
    capture_phy.tx = capture_phy_tx;
    capture_phy.rx = test_phy_rx;
    capture_phy.user_data = &capture;
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &capture_phy,
                                  XGL_DATALINK_MAX_FRAME_SIZE, 100, 1),
              XGL_OK);

    std::vector<uint8_t> payload(512U, 0xA5U);
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = REMOTE_ID;
    params.target_id = FORWARD_ID;
    params.data_type = 1;
    params.payload = payload.data();
    params.payload_len = payload.size();
    params.reliable = true;
    params.ttl = XGL_DEFAULT_TTL;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> frame_buf(
        xgl_frame_serialized_size(payload.size(), frame.extensions_len, 0U));
    size_t frame_len = 0U;
    ASSERT_EQ(xgl_frame_serialize(frame_buf.data(), frame_buf.size(), &frame,
                                  &frame_len),
              XGL_OK);
    ASSERT_GT(frame_len, payload.size());

    EXPECT_EQ(xgl_network_receive(&ctx, nullptr, frame_buf.data(), frame_len),
              XGL_OK);
    EXPECT_EQ(capture.count, 1);
    EXPECT_EQ(allocator_state.alloc_count, 1U);
    EXPECT_EQ(allocator_state.free_count, 1U);

    g_network_forward_allocator_state = nullptr;
}

TEST_F(XglNetworkTest,
       SmallForwardReleasesAllocatorBufferAfterSuccessAndFailure) {
    NetworkForwardAllocatorState allocator_state;
    g_network_forward_allocator_state = &allocator_state;
    xgm_allocator_t allocator = {};
    allocator.ctx = nullptr;
    allocator.alloc = network_forward_test_malloc;
    allocator.free = network_forward_test_free;
    network_ctx.allocator = &allocator;
    xgl_error_t transmit_result = XGL_OK;
    xgl_phy_ops_t forward_phy = {};
    forward_phy.tx = [](const uint8_t*, size_t, void* user_data) {
        return *static_cast<xgl_error_t*>(user_data);
    };
    forward_phy.user_data = &transmit_result;
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &forward_phy, 256,
                                  100, 1),
              XGL_OK);
    const std::vector<uint8_t> bytes = make_frame(REMOTE_ID, FORWARD_ID);

    EXPECT_EQ(
        xgl_network_receive(&network_ctx, nullptr, bytes.data(), bytes.size()),
        XGL_OK);
    EXPECT_EQ(allocator_state.alloc_count, 1U);
    EXPECT_EQ(allocator_state.free_count, 1U);
    transmit_result = XGL_ERR_TX_FAILED;
    EXPECT_EQ(
        xgl_network_receive(&network_ctx, nullptr, bytes.data(), bytes.size()),
        XGL_ERR_TX_FAILED);
    EXPECT_EQ(allocator_state.alloc_count, 2U);
    EXPECT_EQ(allocator_state.free_count, 2U);
    g_network_forward_allocator_state = nullptr;
}

TEST_F(XglNetworkTest, ForwardingPreservesEndToEndAuthTagAfterTtlDecrement) {
    xgl_auth_provider_t provider = {};
    provider.sign = network_test_auth_sign;
    provider.verify = network_test_auth_verify;
    provider.tag_len = 8;
    provider.user_data = nullptr;
    network_ctx.auth_required = true;
    network_ctx.auth_provider = &provider;

    CaptureTx capture;
    xgl_phy_ops_t capture_phy = {};
    capture_phy.tx = capture_phy_tx;
    capture_phy.rx = test_phy_rx;
    capture_phy.user_data = &capture;
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &capture_phy, 256,
                                  100, 1),
              XGL_OK);

    const char payload[] = "signed-hop";
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = REMOTE_ID;
    params.target_id = FORWARD_ID;
    params.data_type = 1;
    params.payload = reinterpret_cast<const uint8_t*>(payload);
    params.payload_len = sizeof(payload) - 1U;
    params.reliable = true;
    params.priority = 0;
    params.ttl = 4;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    xgl_security_ctx_t sender = {};
    ASSERT_EQ(xgl_security_init(&sender, REMOTE_ID, true, &provider), XGL_OK);
    xgl_security_session_config_t sender_session = {};
    sender_session.remote_id = FORWARD_ID;
    sender_session.tx_key_id = 7;
    sender_session.rx_key_id = 7;
    sender_session.tx_nonce_prefix = 1;
    sender_session.rx_nonce_prefix = 2;
    ASSERT_EQ(xgl_security_session_install(&sender, &sender_session), XGL_OK);

    std::vector<uint8_t> encoded(256);
    size_t encoded_len = 0;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded.data(), encoded.size(),
                                                &frame, &sender, &encoded_len),
              XGL_OK);
    encoded.resize(encoded_len);
    xgl_wire_header_t original = {};
    ASSERT_EQ(xgl_wire_decode_header(&original, encoded.data(), encoded.size()),
              XGL_OK);
    const size_t original_tag_offset =
        (size_t)original.header_len + (size_t)original.payload_len;
    std::vector<uint8_t> original_tag(encoded.begin() + original_tag_offset,
                                      encoded.begin() + original_tag_offset +
                                          provider.tag_len);

    ASSERT_EQ(xgl_network_receive(&network_ctx, nullptr, encoded.data(),
                                  encoded.size()),
              XGL_OK);
    ASSERT_EQ(capture.count, 1);
    ASSERT_FALSE(capture.bytes.empty());

    xgl_wire_header_t forwarded = {};
    ASSERT_EQ(xgl_wire_decode_header(&forwarded, capture.bytes.data(),
                                     capture.bytes.size()),
              XGL_OK);
    EXPECT_EQ(forwarded.ttl, 3U);
    const size_t forwarded_tag_offset =
        (size_t)forwarded.header_len + (size_t)forwarded.payload_len;
    ASSERT_LE(forwarded_tag_offset + provider.tag_len,
              capture.bytes.size() - XGL_CRC16_SIZE);
    EXPECT_EQ(std::vector<uint8_t>(capture.bytes.begin() + forwarded_tag_offset,
                                   capture.bytes.begin() +
                                       forwarded_tag_offset + provider.tag_len),
              original_tag);

    xgl_security_ctx_t receiver = {};
    ASSERT_EQ(xgl_security_init(&receiver, FORWARD_ID, true, &provider),
              XGL_OK);
    xgl_security_session_config_t receive_session = sender_session;
    receive_session.remote_id = REMOTE_ID;
    receive_session.tx_nonce_prefix = sender_session.rx_nonce_prefix;
    receive_session.rx_nonce_prefix = sender_session.tx_nonce_prefix;
    ASSERT_EQ(xgl_security_session_install(&receiver, &receive_session),
              XGL_OK);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, capture.bytes.data(),
                                    capture.bytes.size(), nullptr),
              XGL_OK);
    EXPECT_EQ(xgl_security_verify_frame(&receiver, &view), XGL_OK);
}

TEST_F(XglNetworkTest, ReceivePacketNoRouteForForwarding) {
    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, 99);

    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr,
                                          frame_buf.data(), frame_buf.size());
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ReceiveInvalidFrame) {
    uint8_t frame_buf[5] = {0};  // Too short

    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf, 5);
    EXPECT_EQ(err, XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(stats.rx_errors, 1);
}

TEST_F(XglNetworkTest, ReceiveRejectsDuplicateDataTypeExtension) {
    uint8_t ext_buf[XGL_DATA_TYPE_EXT_SIZE * 2U] = {};
    uint8_t data_type_a = 3U;
    uint8_t data_type_b = 4U;
    size_t ext_len = 0U;
    size_t written = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(ext_buf, sizeof(ext_buf),
                                  XGL_WIRE_EXT_DATA_TYPE, &data_type_a, 1U,
                                  &written),
              XGL_OK);
    ext_len += written;
    ASSERT_EQ(xgl_wire_encode_ext(&ext_buf[ext_len], sizeof(ext_buf) - ext_len,
                                  XGL_WIRE_EXT_DATA_TYPE, &data_type_b, 1U,
                                  &written),
              XGL_OK);
    ext_len += written;

    const uint8_t payload[] = {'x'};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = REMOTE_ID;
    params.target_id = LOCAL_ID;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.extensions = ext_buf;
    params.extensions_len = ext_len;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.ttl = XGL_DEFAULT_TTL;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> frame_buf(xgl_frame_calculate_size(sizeof(payload)) +
                                   ext_len);
    size_t frame_len = 0U;
    ASSERT_EQ(xgl_frame_serialize(frame_buf.data(), frame_buf.size(), &frame,
                                  &frame_len),
              XGL_OK);

    EXPECT_EQ(
        xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_len),
        XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(stats.rx_errors, 1);
}

TEST_F(XglNetworkTest, ReceiveLocalPacketPropagatesSessionExtensionEpoch) {
    uint8_t session_value[12] = {};
    size_t session_value_len = 0U;
    ASSERT_EQ(xgl_wire_encode_session_ext_value(
                  session_value, sizeof(session_value), 0x01020304U,
                  0xAABBCCDDEEFF0011ULL, &session_value_len),
              XGL_OK);

    uint8_t session_ext[16] = {};
    size_t session_ext_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(session_ext, sizeof(session_ext),
                                  XGL_WIRE_EXT_SESSION, session_value,
                                  session_value_len, &session_ext_len),
              XGL_OK);

    const uint8_t payload[] = {'s'};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = REMOTE_ID;
    params.target_id = LOCAL_ID;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.connection_id = 0x11223344U;
    params.packet_number = 9U;
    params.extensions = session_ext;
    params.extensions_len = session_ext_len;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.ttl = XGL_DEFAULT_TTL;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> frame_buf(
        xgl_frame_serialized_size(sizeof(payload), session_ext_len, 0U));
    size_t frame_len = 0U;
    ASSERT_EQ(xgl_frame_serialize(frame_buf.data(), frame_buf.size(), &frame,
                                  &frame_len),
              XGL_OK);

    struct CapturedPacket {
        bool called = false;
        xgl_packet_t packet = {};
    } capture;

    xgl_packet_interface_t upper_layer = {};
    upper_layer.ctx = &capture;
    upper_layer.receive = [](void* ctx, xgl_handle_t handle,
                             const xgl_packet_t* data) -> xgl_error_t {
        (void)handle;
        auto* capture = static_cast<CapturedPacket*>(ctx);
        auto* packet = static_cast<const xgl_packet_t*>(data);
        if (capture == nullptr || packet == nullptr) {
            return XGL_ERR_NULL_POINTER;
        }
        capture->called = true;
        capture->packet = *packet;
        return XGL_OK;
    };
    network_ctx.upper_layer = &upper_layer;

    ASSERT_EQ(
        xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_len),
        XGL_OK);
    ASSERT_TRUE(capture.called);
    EXPECT_EQ(capture.packet.connection_id, 0x11223344U);
    EXPECT_EQ(capture.packet.session_epoch, 0x01020304U);
}

TEST_F(XglNetworkTest, ReceiveLocalDataPacketDoesNotTreatControlFlagAsAckOnly) {
    const uint8_t payload[] = {'f'};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = REMOTE_ID;
    params.target_id = LOCAL_ID;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.flags = XGL_WIRE_FLAG_CONTROL;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.ttl = XGL_DEFAULT_TTL;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> frame_buf(
        xgl_frame_serialized_size(sizeof(payload), 0U, 0U));
    size_t frame_len = 0U;
    ASSERT_EQ(xgl_frame_serialize(frame_buf.data(), frame_buf.size(), &frame,
                                  &frame_len),
              XGL_OK);

    struct CapturedPacket {
        bool called = false;
        xgl_packet_t packet = {};
    } capture;

    xgl_packet_interface_t upper_layer = {};
    upper_layer.ctx = &capture;
    upper_layer.receive = [](void* ctx, xgl_handle_t handle,
                             const xgl_packet_t* data) -> xgl_error_t {
        (void)handle;
        auto* capture = static_cast<CapturedPacket*>(ctx);
        auto* packet = static_cast<const xgl_packet_t*>(data);
        if (capture == nullptr || packet == nullptr) {
            return XGL_ERR_NULL_POINTER;
        }
        capture->called = true;
        capture->packet = *packet;
        return XGL_OK;
    };
    network_ctx.upper_layer = &upper_layer;

    ASSERT_EQ(
        xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_len),
        XGL_OK);
    ASSERT_TRUE(capture.called);
    EXPECT_EQ(capture.packet.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(capture.packet.reliable, XGL_RELIABILITY_NONE);
}

TEST_F(XglNetworkTest, FrameMetadataDecodesTransportPacketSemantics) {
    uint8_t data_type = 0x7AU;
    uint8_t data_type_ext[XGL_DATA_TYPE_EXT_SIZE] = {};
    size_t data_type_ext_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(data_type_ext, sizeof(data_type_ext),
                                  XGL_WIRE_EXT_DATA_TYPE, &data_type, 1U,
                                  &data_type_ext_len),
              XGL_OK);

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

    uint8_t extensions[XGL_DATA_TYPE_EXT_SIZE + XGL_SESSION_EXT_SIZE] = {};
    memcpy(extensions, data_type_ext, data_type_ext_len);
    memcpy(&extensions[data_type_ext_len], session_ext, session_ext_len);
    size_t extensions_len = data_type_ext_len + session_ext_len;

    const uint8_t payload[] = {'m', 'e', 't', 'a'};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = REMOTE_ID;
    params.target_id = LOCAL_ID;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.connection_id = 0x10203040U;
    params.packet_number = 99U;
    params.session_epoch = 0x01020304U;
    params.extensions = extensions;
    params.extensions_len = extensions_len;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.fragment = true;
    params.priority = 5;
    params.ttl = 3;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> frame_buf(
        xgl_frame_serialized_size(sizeof(payload), extensions_len, 0U));
    size_t frame_len = 0U;
    ASSERT_EQ(xgl_frame_serialize(frame_buf.data(), frame_buf.size(), &frame,
                                  &frame_len),
              XGL_OK);

    xgl_wire_frame_view_t metadata = {};
    ASSERT_EQ(
        xgl_wire_decode_frame(&metadata, frame_buf.data(), frame_len, nullptr),
        XGL_OK);

    EXPECT_EQ(metadata.header.source_id, REMOTE_ID);
    EXPECT_EQ(metadata.header.target_id, LOCAL_ID);
    EXPECT_EQ(metadata.header.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(metadata.header.connection_id, 0x10203040U);
    EXPECT_EQ(metadata.header.packet_number, 99U);
    EXPECT_EQ(metadata.data_type, data_type);
    EXPECT_EQ(metadata.session_epoch, 0x01020304U);
    EXPECT_EQ(metadata.reliable, XGL_RELIABILITY_ACK_ELICITING);
    EXPECT_EQ(metadata.fragment, 1U);
    EXPECT_EQ(metadata.priority, 5U);
    ASSERT_EQ(metadata.payload_len, sizeof(payload));
    EXPECT_EQ(std::vector<uint8_t>(metadata.payload,
                                   metadata.payload + metadata.payload_len),
              std::vector<uint8_t>(payload, payload + sizeof(payload)));
    EXPECT_EQ(metadata.extensions_len, extensions_len);
}

TEST_F(XglNetworkTest, FrameMetadataRejectsDuplicateDataTypeExtensions) {
    uint8_t ext_buf[XGL_DATA_TYPE_EXT_SIZE * 2U] = {};
    uint8_t first_type = 1U;
    uint8_t second_type = 2U;
    size_t first_len = 0U;
    size_t second_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(ext_buf, sizeof(ext_buf),
                                  XGL_WIRE_EXT_DATA_TYPE, &first_type, 1U,
                                  &first_len),
              XGL_OK);
    ASSERT_EQ(xgl_wire_encode_ext(
                  &ext_buf[first_len], sizeof(ext_buf) - first_len,
                  XGL_WIRE_EXT_DATA_TYPE, &second_type, 1U, &second_len),
              XGL_OK);

    const uint8_t payload[] = {'d'};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = REMOTE_ID;
    params.target_id = LOCAL_ID;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.extensions = ext_buf;
    params.extensions_len = first_len + second_len;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.ttl = XGL_DEFAULT_TTL;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> frame_buf(
        xgl_frame_serialized_size(sizeof(payload), params.extensions_len, 0U));
    size_t frame_len = 0U;
    ASSERT_EQ(xgl_frame_serialize(frame_buf.data(), frame_buf.size(), &frame,
                                  &frame_len),
              XGL_OK);

    xgl_wire_frame_view_t metadata = {};
    EXPECT_EQ(
        xgl_wire_decode_frame(&metadata, frame_buf.data(), frame_len, nullptr),
        XGL_ERR_INVALID_FRAME);
}

/*---------------------------------------------------------------------------*/
/* Error Callback Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ErrorCallbackInvoked) {
    bool callback_invoked = false;
    xgl_error_t callback_error = XGL_OK;

    auto error_cb = [](xgl_handle_t handle, xgl_error_t error,
                       const char* message, void* user_data) {
        (void)handle;
        (void)message;
        bool* invoked = (bool*)user_data;
        *invoked = true;
    };

    network_ctx.error_callback = error_cb;
    network_ctx.callback_user_data = &callback_invoked;

    xgl_packet_data_t packet_data = {};
    packet_data.data_len = 10;
    packet_data.data = (uint8_t*)"test_data";

    xgl_packet_t packet = {};
    packet.source_id = LOCAL_ID;
    packet.target_id = 99;
    // No route
    packet.data_type = 1;
    packet.reliable = 1;
    packet.priority = 0;
    packet.data = &packet_data;

    xgl_network_send(&network_ctx, &packet);
    EXPECT_TRUE(callback_invoked);
}

/*---------------------------------------------------------------------------*/
/* Layer Interface Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, GetTypedInterfaces) {
    xgl_packet_interface_t packets = {};
    xgl_frame_interface_t frames = {};
    ASSERT_EQ(xgl_network_get_interfaces(&network_ctx, &packets, &frames),
              XGL_OK);
    EXPECT_EQ(packets.ctx, &network_ctx);
    EXPECT_NE(packets.send, nullptr);
    EXPECT_EQ(packets.receive, nullptr);
    EXPECT_EQ(frames.ctx, &network_ctx);
    EXPECT_EQ(frames.send, nullptr);
    EXPECT_NE(frames.receive, nullptr);
}

TEST_F(XglNetworkTest, GetTypedInterfacesRejectsNullPointer) {
    EXPECT_EQ(xgl_network_get_interfaces(nullptr, nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
}
