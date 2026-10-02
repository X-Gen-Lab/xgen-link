/**
 * \file            test_send_policy.cpp
 * \brief           Shared DATA admission across public send paths
 */

#include "test_host_allocator.h"

#include <gtest/gtest.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

class XglSendPolicyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        xgl_config_get_preset_boot(&config);
        config.source_id = 1U;
        config.protocol.max_frame_size = 128U;
        config.memory.rx_buffer_size = 128U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.error_callback = Error;
        config.callback_user_data = this;
        xgl_test_use_host_allocator(&config);
        phy = {Transmit, Receive, this};
        route = {2U, &phy, 128U, 100U, 1U};
        provider = {Sign, Verify, 4U, this};
        payload.fill(0x5AU);
    }

    void TearDown() override {
        xgl_destroy(handle);
    }

    void Initialize(bool fragmented = false, bool authenticated = false,
                    uint16_t frame_size = 128U, size_t message_limit = 32U) {
        config.protocol.max_frame_size = frame_size;
        route.max_frame_size = frame_size;
        config.auth_required = authenticated;
        config.auth_provider = authenticated ? &provider : nullptr;
        config.features.enable_fragmentation = fragmented;
        if (fragmented) {
            config.features.max_message_size = message_limit;
            config.features.max_reassembly_slots = 1U;
            config.features.max_reassembly_bytes = message_limit;
            config.features.max_tx_message_bytes = message_limit;
        }
        ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
        handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr);
        ASSERT_EQ(xgl_init(handle), XGL_OK);
        if (authenticated) {
            xgl_security_session_config_t session = {};
            session.remote_id = 2U;
            session.tx_key_id = 7U;
            session.rx_key_id = 7U;
            session.tx_nonce_prefix = 1U;
            session.rx_nonce_prefix = 2U;
            ASSERT_EQ(xgl_install_security_session(handle, &session), XGL_OK);
        }
    }

    xgl_tx_data_t Ordinary(size_t length, bool reliable = false) {
        xgl_tx_data_t tx = {};
        tx.target_id = 2U;
        tx.data = payload.data();
        tx.data_len = length;
        tx.reliable = reliable;
        tx.timeout_ms = 10U;
        return tx;
    }

    xgl_tx_zerocopy_request_t CallerFrame(size_t length) {
        xgl_tx_zerocopy_request_t request = {};
        request.data.target_id = 2U;
        request.data.data_len = length;
        request.data.buffer = frame.data();
        request.data.buffer_size = frame.size();
        request.data.data_offset = XGL_FRAME_HEADER_SIZE;
        std::memcpy(frame.data() + request.data.data_offset, payload.data(),
                    length);
        return request;
    }

    void FailDefaultScope() {
        auto tx = Ordinary(1U, true);
        ASSERT_EQ(xgl_send_at(handle, &tx, 0U), XGL_OK);
        fail_all_tx = true;
        const xgl_work_budget_t budget = {0U, 100U};
        ASSERT_EQ(xgl_step(handle, 10U, &budget), XGL_ERR_TX_FAILED);
        fail_all_tx = false;
        ASSERT_EQ(xgl_send_at(handle, &tx, 11U), XGL_ERR_ACK_TIMEOUT);
    }

    void ExpectStatsUnchanged(const xgl_statistics_t& before) {
#if XGL_FEATURE_STATISTICS
        xgl_statistics_t after = {};
        ASSERT_EQ(xgl_stats_get(handle, &after), XGL_OK);
        EXPECT_EQ(after.transport.tx_packets, before.transport.tx_packets);
        EXPECT_EQ(after.transport.tx_bytes, before.transport.tx_bytes);
        EXPECT_EQ(after.transport.tx_errors, before.transport.tx_errors);
        EXPECT_EQ(after.network.tx_packets, before.network.tx_packets);
        EXPECT_EQ(after.network.tx_bytes, before.network.tx_bytes);
        EXPECT_EQ(after.network.tx_errors, before.network.tx_errors);
        EXPECT_EQ(after.datalink.tx_packets, before.datalink.tx_packets);
        EXPECT_EQ(after.datalink.tx_bytes, before.datalink.tx_bytes);
        EXPECT_EQ(after.datalink.tx_errors, before.datalink.tx_errors);
        EXPECT_EQ(after.memory_used, before.memory_used);
#else
        (void)before;
#endif
    }

    xgl_statistics_t Snapshot() {
        xgl_statistics_t stats = {};
#if XGL_FEATURE_STATISTICS
        EXPECT_EQ(xgl_stats_get(handle, &stats), XGL_OK);
#endif
        return stats;
    }

    static xgl_error_t Transmit(const uint8_t* bytes, size_t length,
                                void* context) {
        auto& self = *static_cast<XglSendPolicyTest*>(context);
        xgl_wire_frame_view_t view = {};
        xgl_error_t decoded =
            xgl_wire_decode_frame(&view, bytes, length, nullptr);
        if (decoded != XGL_OK) {
            return decoded;
        }
        ++self.tx_calls;
        if (view.header.packet_type == XGL_PACKET_TYPE_DATA) {
            ++self.data_calls;
            self.data_numbers.push_back(view.header.packet_number);
            self.data_connections.push_back(view.header.connection_id);
            self.data_epochs.push_back(view.session_epoch);
        }
        const bool selected_data_failure =
            view.header.packet_type == XGL_PACKET_TYPE_DATA &&
            self.fail_data_call == self.data_calls;
        return self.fail_all_tx || selected_data_failure ? XGL_ERR_TX_FAILED
                                                         : XGL_OK;
    }

    static xgl_error_t Receive(uint8_t*, size_t* length, void*) {
        *length = 0U;
        return XGL_OK;
    }

    static xgl_error_t Sign(const xgl_auth_input_t*, uint8_t* tag,
                            size_t capacity, size_t* length, void* context) {
        auto& self = *static_cast<XglSendPolicyTest*>(context);
        ++self.sign_calls;
        if (capacity < 4U) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        std::memset(tag, 0xA5U, 4U);
        *length = 4U;
        return XGL_OK;
    }

    static xgl_error_t Verify(const xgl_auth_input_t*, const uint8_t*, size_t,
                              bool* valid, void*) {
        *valid = true;
        return XGL_OK;
    }

    static void Error(xgl_handle_t, xgl_error_t error, const char*,
                      void* context) {
        auto& self = *static_cast<XglSendPolicyTest*>(context);
        self.errors.push_back(error);
    }

    xgl_config_t config = {};
    xgl_handle_t handle = nullptr;
    xgl_phy_ops_t phy = {};
    xgl_route_item_t route = {};
    xgl_auth_provider_t provider = {};
    std::array<uint8_t, 96U> payload = {};
    std::array<uint8_t, 128U> frame = {};
    size_t tx_calls = 0U;
    size_t data_calls = 0U;
    size_t sign_calls = 0U;
    size_t fail_data_call = 0U;
    bool fail_all_tx = false;
    std::vector<xgl_error_t> errors;
    std::vector<uint32_t> data_numbers;
    std::vector<uint32_t> data_connections;
    std::vector<uint32_t> data_epochs;
};

#if XGL_FEATURE_FRAGMENTATION
TEST_F(XglSendPolicyTest, OrdinaryRejectsSingleFrameAboveMessageLimit) {
    Initialize(true);
    auto tx = Ordinary(33U);
    const auto before = Snapshot();
    EXPECT_EQ(xgl_send_at(handle, &tx, 0U), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(tx_calls, 0U);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, LegacyZeroCopyRejectsSingleFrameAboveMessageLimit) {
    Initialize(true);
    auto request = CallerFrame(33U);
    const auto before = Snapshot();
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &request.data, 0U),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(tx_calls, 0U);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, ScopedZeroCopyRejectsSingleFrameAboveMessageLimit) {
    Initialize(true);
    auto request = CallerFrame(33U);
    const auto before = Snapshot();
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &request, 0U),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(tx_calls, 0U);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, LayoutRejectsSingleFrameAboveMessageLimit) {
    Initialize(true);
    auto request = CallerFrame(33U);
    xgl_tx_frame_layout_t layout = {19U, 23U};
    const auto before = Snapshot();
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(layout.payload_offset, 0U);
    EXPECT_EQ(layout.frame_size, 0U);
    EXPECT_EQ(tx_calls, 0U);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, AllSendPathsAcceptMessageLimitBoundary) {
    Initialize(true);
    for (size_t length : {31U, 32U}) {
        SCOPED_TRACE(length);
        auto tx = Ordinary(length);
        ASSERT_EQ(xgl_send_at(handle, &tx, 0U), XGL_OK);
        auto request = CallerFrame(length);
        xgl_tx_frame_layout_t layout = {};
        ASSERT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout), XGL_OK);
        request.data.data_offset = layout.payload_offset;
        ASSERT_EQ(xgl_send_zerocopy_at(handle, &request.data, 0U), XGL_OK);
        ASSERT_EQ(xgl_send_zerocopy_scoped_at(handle, &request, 0U), XGL_OK);
    }
    EXPECT_EQ(data_calls, 6U);
}

TEST_F(XglSendPolicyTest,
       FirstReliableFragmentHardFailureIsAcceptedAndTerminal) {
    Initialize(true, false, 64U, 96U);
    fail_data_call = 1U;
    auto tx = Ordinary(64U, true);
    EXPECT_EQ(xgl_send_at(handle, &tx, 0U), XGL_OK);
    EXPECT_EQ(data_calls, 1U);
    EXPECT_NE(std::find(errors.begin(), errors.end(), XGL_ERR_TX_FAILED),
              errors.end());
    const auto before = Snapshot();
    EXPECT_EQ(xgl_send_at(handle, &tx, 1U), XGL_ERR_ACK_TIMEOUT);
    tx.reliable = false;
    EXPECT_EQ(xgl_send_at(handle, &tx, 1U), XGL_ERR_ACK_TIMEOUT);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, UnreliableFragmentFailurePreservesEarlierSubmission) {
    Initialize(true, false, 64U, 96U);
    fail_data_call = 2U;
    auto tx = Ordinary(64U);
    EXPECT_EQ(xgl_send_at(handle, &tx, 0U), XGL_ERR_TX_FAILED);
    EXPECT_EQ(data_calls, 2U);
    ASSERT_EQ(data_numbers.size(), 2U);
    EXPECT_EQ(data_numbers[0], 0U);
    EXPECT_EQ(data_numbers[1], 0U);
#if XGL_FEATURE_STATISTICS
    const auto stats = Snapshot();
    EXPECT_EQ(stats.network.tx_packets, 1U);
    EXPECT_GT(stats.network.tx_bytes, 0U);
    EXPECT_EQ(stats.transport.tx_packets, 0U);
    EXPECT_EQ(stats.transport.tx_errors, 1U);
#endif
}
#endif

TEST_F(XglSendPolicyTest, FailedScopeRejectsOrdinaryUnreliableData) {
    Initialize();
    FailDefaultScope();
    const size_t submitted = tx_calls;
    const auto before = Snapshot();
    auto tx = Ordinary(1U);
    EXPECT_EQ(xgl_send_at(handle, &tx, 11U), XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(tx_calls, submitted);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, FailedScopeRejectsLegacyZeroCopyData) {
    Initialize();
    FailDefaultScope();
    const size_t submitted = tx_calls;
    const auto before = Snapshot();
    auto request = CallerFrame(1U);
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &request.data, 11U),
              XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(tx_calls, submitted);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, FailedScopeRejectsScopedZeroCopyData) {
    Initialize();
    FailDefaultScope();
    const size_t submitted = tx_calls;
    const auto before = Snapshot();
    auto request = CallerFrame(1U);
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &request, 11U),
              XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(tx_calls, submitted);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, FailedScopeLayoutIsClearedWithoutSideEffects) {
    Initialize();
    FailDefaultScope();
    const size_t submitted = tx_calls;
    const auto before = Snapshot();
    auto request = CallerFrame(1U);
    xgl_tx_frame_layout_t layout = {19U, 23U};
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout),
              XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(layout.payload_offset, 0U);
    EXPECT_EQ(layout.frame_size, 0U);
    EXPECT_EQ(tx_calls, submitted);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, FailedScopeDoesNotBlockAnotherConnectionOrEpoch) {
    Initialize();
    FailDefaultScope();
    auto tx = Ordinary(1U);
    tx.connection_id = 7U;
    ASSERT_EQ(xgl_send_at(handle, &tx, 11U), XGL_OK);
    auto request = CallerFrame(1U);
    request.session_epoch = 9U;
    xgl_tx_frame_layout_t layout = {};
    ASSERT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout), XGL_OK);
    request.data.data_offset = layout.payload_offset;
    frame[layout.payload_offset] = payload[0];
    ASSERT_EQ(xgl_send_zerocopy_scoped_at(handle, &request, 11U), XGL_OK);
    EXPECT_EQ(data_connections.back(), 0U);
    EXPECT_EQ(data_epochs.back(), 9U);
    tx.connection_id = 0U;
    tx.reliable = true;
    EXPECT_EQ(xgl_send_at(handle, &tx, 12U), XGL_ERR_ACK_TIMEOUT);
}

TEST_F(XglSendPolicyTest, ZeroCopyValidatesRequestsBeforeTerminalPolicy) {
    Initialize();
    FailDefaultScope();
    auto request = CallerFrame(1U);
    request.data.buffer = nullptr;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &request.data, 11U),
              XGL_ERR_NULL_POINTER);
    request.data.buffer = frame.data();
    request.data.priority = 8U;
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &request, 11U),
              XGL_ERR_INVALID_PARAM);
    xgl_tx_frame_layout_t layout = {};
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout),
              XGL_ERR_INVALID_PARAM);
    request.data.priority = 0U;
    request.data.reliable = true;
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, &request, 11U),
              XGL_ERR_INVALID_PARAM);
}

#if XGL_FEATURE_AUTH && XGL_FEATURE_FRAGMENTATION
TEST_F(XglSendPolicyTest, AuthenticatedPolicyQueriesNeverSignOrSubmit) {
    Initialize(true, true);
    auto request = CallerFrame(32U);
    xgl_tx_frame_layout_t layout = {};
    const auto before = Snapshot();
    ASSERT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout), XGL_OK);
    ASSERT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout), XGL_OK);
    request.data.data_len = 33U;
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(layout.frame_size, 0U);
    EXPECT_EQ(sign_calls, 0U);
    EXPECT_EQ(tx_calls, 0U);
    ExpectStatsUnchanged(before);
}

TEST_F(XglSendPolicyTest, FailedAuthenticatedScopeQueryDoesNotConsumeSecurity) {
    Initialize(false, true);
    FailDefaultScope();
    const size_t signed_before = sign_calls;
    const size_t submitted = tx_calls;
    const auto before = Snapshot();
    auto request = CallerFrame(1U);
    xgl_tx_frame_layout_t layout = {19U, 23U};
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout),
              XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(layout.frame_size, 0U);
    EXPECT_EQ(sign_calls, signed_before);
    EXPECT_EQ(tx_calls, submitted);
    ExpectStatsUnchanged(before);
}
#endif
