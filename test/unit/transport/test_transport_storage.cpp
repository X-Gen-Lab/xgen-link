/**
 * \file            test_transport_storage.cpp
 * \brief           Bounded receive ownership and inline window reservations
 * \author          X-Gen Lab
 */

#include <transport/xgl_transport_internal.h>
#include <xgen/memory/libc_allocator.h>
#include <xgl/xgl.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <map>

TEST(XglTransportStorageTest, WorkspaceDoesNotReserveWindowAllocations) {
    xgl_config_t config = {};
    xgl_config_get_preset_boot(&config);
    config.memory.allocator = xgm_allocator_libc();
    xgl_memory_report_t report = {};
    ASSERT_EQ(xgl_memory_report(&config, &report), XGL_OK);
    EXPECT_EQ(report.window_bytes, 0U);
}

#if XGL_FEATURE_OUT_OF_ORDER
namespace {

/** \brief           Observe allocation ownership and inject a bounded failure.
 */
struct StorageAllocator {
    size_t calls = 0U;
    size_t fail_call = 0U;
    size_t releases = 0U;
    std::map<void*, size_t> live;

    static void* Allocate(void* context, size_t size) {
        auto* self = static_cast<StorageAllocator*>(context);
        ++self->calls;
        if (self->fail_call == self->calls) {
            return nullptr;
        }
        void* storage = std::malloc(size);
        if (storage != nullptr) {
            self->live.emplace(storage, size);
        }
        return storage;
    }

    static void Release(void* context, void* storage) {
        auto* self = static_cast<StorageAllocator*>(context);
        if (storage == nullptr) {
            return;
        }
        const auto found = self->live.find(storage);
        EXPECT_NE(found, self->live.end());
        if (found != self->live.end()) {
            self->live.erase(found);
            ++self->releases;
            std::free(storage);
        }
    }
};

class XglTransportBufferedStorageTest : public ::testing::Test {
  protected:
    StorageAllocator allocations;
    xgm_allocator_t allocator = {&allocations, StorageAllocator::Allocate,
                                 StorageAllocator::Release};
    xgl_transport_ctx_t context = {};
    xgl_transport_peer_state_t peer = {};
    xgl_layer_stats_t statistics = {};
    std::array<uint8_t, 3> payload = {0x11U, 0x22U, 0x33U};
    std::array<uint8_t, 4> extensions = {0x7EU, 0x02U, 0xA5U, 0x5AU};
    xgl_packet_data_t data = {};
    xgl_packet_t packet = {};

    void SetUp() override {
        xgl_transport_config_t config = {};
        config.allocator = &allocator;
        config.stats = &statistics;
        config.max_peers = 1U;
        config.max_tx_packets = 4U;
        config.max_rx_buffered_packets = 4U;
        config.window_size = 4U;
        config.max_frame_size = 128U;
        ASSERT_EQ(xgl_transport_init(&context, &config), XGL_OK);
        context.peers = &peer;
        peer.tx_window.window_size = 4U;
        data.data = payload.data();
        data.data_len = payload.size();
        packet.data = &data;
        packet.extensions = extensions.data();
        packet.extensions_len = extensions.size();
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.connection_id = 7U;
        packet.session_epoch = 9U;
    }

    void TearDown() override {
        transport_clear_rx_buffered(&context, &peer);
        context.peers = nullptr;
        xgl_transport_destroy(&context);
        EXPECT_TRUE(allocations.live.empty());
        for (const auto& allocation : allocations.live) {
            std::free(allocation.first);
        }
    }

    xgl_error_t Cache(uint32_t number) {
        return transport_cache_out_of_order_packet(&context, &peer, &packet,
                                                   number);
    }
};

} /* namespace */

TEST_F(XglTransportBufferedStorageTest, PayloadAndUnknownTlvUseOneOwnedSpan) {
    allocations.fail_call = 3U;
    ASSERT_EQ(Cache(1U), XGL_OK);
    EXPECT_EQ(allocations.calls, 2U);
    ASSERT_NE(peer.rx_buffered, nullptr);
    const auto* buffered = peer.rx_buffered;
    EXPECT_EQ(buffered->packet.connection_id, 7U);
    EXPECT_EQ(buffered->packet.session_epoch, 9U);
    EXPECT_EQ(buffered->packet.packet_number, 1U);
    EXPECT_EQ(buffered->packet.extensions,
              buffered->packet.data->data + payload.size());

    payload.fill(0U);
    extensions.fill(0U);
    const std::array<uint8_t, 3> expected_payload = {0x11U, 0x22U, 0x33U};
    const std::array<uint8_t, 4> expected_tlv = {0x7EU, 0x02U, 0xA5U, 0x5AU};
    EXPECT_EQ(std::memcmp(buffered->packet.data->data, expected_payload.data(),
                          expected_payload.size()),
              0);
    EXPECT_EQ(std::memcmp(buffered->packet.extensions, expected_tlv.data(),
                          expected_tlv.size()),
              0);
    transport_clear_rx_buffered(&context, &peer);
    EXPECT_EQ(allocations.releases, 2U);
    EXPECT_EQ(peer.rx_buffered_count, 0U);
}

TEST_F(XglTransportBufferedStorageTest, SpanFailureRollsBackOnlyNewRecord) {
    allocations.fail_call = 2U;
    EXPECT_EQ(Cache(1U), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(peer.rx_buffered, nullptr);
    EXPECT_EQ(peer.rx_buffered_count, 0U);
    EXPECT_EQ(allocations.releases, 1U);
    EXPECT_TRUE(allocations.live.empty());
    allocations.fail_call = 0U;
    EXPECT_EQ(Cache(1U), XGL_OK);
}

TEST_F(XglTransportBufferedStorageTest, RecordFailureDoesNotAllocateBytes) {
    allocations.fail_call = 1U;
    EXPECT_EQ(Cache(1U), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(allocations.calls, 1U);
    EXPECT_EQ(peer.rx_buffered_count, 0U);
    EXPECT_TRUE(allocations.live.empty());
}

TEST_F(XglTransportBufferedStorageTest, RejectsMissingTlvBeforeAllocation) {
    packet.extensions = nullptr;
    EXPECT_EQ(Cache(1U), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(allocations.calls, 0U);
}

TEST_F(XglTransportBufferedStorageTest, RejectsCombinedSpanOverFrameBudget) {
    context.max_frame_size = 32U;
    EXPECT_EQ(Cache(1U), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(allocations.calls, 0U);
}

TEST_F(XglTransportBufferedStorageTest, RejectsCombinedLengthOverflow) {
    packet.extensions_len = SIZE_MAX;
    allocations.fail_call = 1U;
    EXPECT_EQ(Cache(1U), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(allocations.calls, 0U);
}

TEST_F(XglTransportBufferedStorageTest,
       OrderedRemovalAndDuplicateKeepOwnership) {
    ASSERT_EQ(Cache(3U), XGL_OK);
    ASSERT_EQ(Cache(1U), XGL_OK);
    ASSERT_EQ(Cache(2U), XGL_OK);
    const size_t calls = allocations.calls;
    ASSERT_EQ(Cache(2U), XGL_OK);
    EXPECT_EQ(allocations.calls, calls);
    EXPECT_EQ(peer.rx_buffered_count, 3U);

    auto* middle = transport_take_rx_buffered(&peer, 2U);
    ASSERT_NE(middle, nullptr);
    EXPECT_EQ(middle->next, nullptr);
    EXPECT_EQ(middle->packet.packet_number, 2U);
    EXPECT_EQ(peer.rx_buffered->packet.packet_number, 1U);
    ASSERT_NE(peer.rx_buffered->next, nullptr);
    EXPECT_EQ(peer.rx_buffered->next->packet.packet_number, 3U);
    EXPECT_EQ(transport_take_rx_buffered(&peer, 2U), nullptr);
    transport_free_rx_buffered_packet(&context, middle);
    transport_clear_rx_buffered(&context, &peer);
    EXPECT_TRUE(allocations.live.empty());
}

TEST_F(XglTransportBufferedStorageTest, PayloadOnlyPacketHasNoExtensionView) {
    packet.extensions = nullptr;
    packet.extensions_len = 0U;
    ASSERT_EQ(Cache(1U), XGL_OK);
    ASSERT_NE(peer.rx_buffered, nullptr);
    EXPECT_EQ(peer.rx_buffered->packet.extensions, nullptr);
    EXPECT_EQ(peer.rx_buffered->packet.extensions_len, 0U);
    EXPECT_EQ(allocations.calls, 2U);
}
#endif
