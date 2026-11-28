/**
 * \file            test_transport_tx_contracts.cpp
 * \brief           Transactional first-send admission across build profiles
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_host_allocator.h"
#include <api/xgl_instance_internal.h>
#include <network/xgl_route.h>
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>

#include <gtest/gtest.h>

namespace {

struct FailingResource {
    const xgl_allocator_t* upstream = nullptr;
    bool reject = true;
    xgl_allocator_t service = {};

    static void* Allocate(void* context, size_t size) {
        auto& self = *static_cast<FailingResource*>(context);
        return self.reject ? nullptr : xgl_alloc(self.upstream, size);
    }

    static void Release(void* context, void* storage) {
        auto& self = *static_cast<FailingResource*>(context);
        xgl_free(self.upstream, storage);
    }

    void Replace(const xgl_allocator_t*& resource) {
        upstream = resource;
        service.ctx = this;
        service.alloc = Allocate;
        service.free = Release;
        resource = &service;
    }
};

class XglTransportTxContractTest : public ::testing::Test {
  protected:
    xgl_handle_t instance = nullptr;
    xgl_phy_ops_t phy = {};
    xgl_route_item_t route = {};
    xgl_config_t config = {};
    FailingResource resource;
    xgl_error_t hello_result = XGL_OK;
    xgl_error_t data_result = XGL_OK;
    unsigned hello_calls = 0U;
    unsigned data_calls = 0U;
    uint32_t last_data_number = UINT32_MAX;
    uint8_t payload = 0x42U;

    static xgl_error_t Transmit(const uint8_t* bytes, size_t length,
                                void* context) {
        auto& self = *static_cast<XglTransportTxContractTest*>(context);
        xgl_wire_frame_view_t frame = {};
        const xgl_error_t error =
            xgl_wire_decode_frame(&frame, bytes, length, nullptr);
        EXPECT_EQ(error, XGL_OK);
        if (error != XGL_OK) {
            return error;
        }
        if (frame.header.packet_type == XGL_PACKET_TYPE_CONTROL) {
            ++self.hello_calls;
            return self.hello_result;
        }
        if (frame.header.packet_type == XGL_PACKET_TYPE_DATA) {
            ++self.data_calls;
            self.last_data_number = frame.header.packet_number;
            return self.data_result;
        }
        return XGL_OK;
    }

    static xgl_error_t Receive(uint8_t*, size_t* length, void*) {
        *length = 0U;
        return XGL_OK;
    }

    void SetUp() override {
        phy = {Transmit, Receive, this};
        route = {2U, &phy, 128U, 1000U, 0U};
        xgl_config_get_preset_boot(&config);
        config.features.peer_idle_timeout_ms = 0U;
        config.memory.allocator = xgl_test_host_allocator();
        config.route_table = &route;
        config.route_table_len = 1U;
        instance = xgl_create(&config);
        ASSERT_NE(instance, nullptr);
        ASSERT_EQ(xgl_init(instance), XGL_OK);
    }

    void TearDown() override {
        xgl_destroy(instance);
    }

    xgl_transport_ctx_t& Transport() {
        return instance->layers.transport_ctx;
    }

    xgl_error_t Send(uint16_t target = 2U, uint32_t connection = 0U,
                     bool reliable = true) {
        xgl_tx_data_t request = {};
        request.target_id = target;
        request.connection_id = connection;
        request.data = &payload;
        request.data_len = 1U;
        request.reliable = reliable;
        return xgl_send_at(instance, &request, 0U);
    }

    void Acknowledge(uint32_t number) {
        uint8_t value[16] = {}, extension[32] = {};
        size_t value_length = 0U, extension_length = 0U;
        const xgl_wire_ack_range_t range = {0U, 1U};
        ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(value, sizeof(value),
                                                      number, 0U, &range, 1U,
                                                      &value_length),
                  XGL_OK);
        ASSERT_EQ(xgl_wire_encode_ext(extension, sizeof(extension),
                                      XGL_WIRE_EXT_ACK_RANGE, value,
                                      value_length, &extension_length),
                  XGL_OK);
        xgl_packet_t ack = {};
        ack.source_id = 2U;
        ack.target_id = 1U;
        ack.packet_type = XGL_PACKET_TYPE_ACK;
        ack.reliable = XGL_RELIABILITY_ACK_ONLY;
        ack.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        ack.extensions = extension;
        ack.extensions_len = extension_length;
        ASSERT_EQ(xgl_transport_receive(&Transport(), instance, &ack), XGL_OK);
    }

    void ExpectFreshScopeCanSend() {
        EXPECT_EQ(Transport().peers, nullptr);
        ASSERT_EQ(Send(2U, 7U), XGL_OK);
        EXPECT_EQ(last_data_number, 0U);
    }

    void Hello(uint32_t now, uint32_t connection = 7U) {
        Transport().current_time_ms = now;
        xgl_packet_t hello = {};
        hello.source_id = 2U;
        hello.target_id = 1U;
        hello.connection_id = connection;
        hello.packet_type = XGL_PACKET_TYPE_CONTROL;
        hello.data_type = XGL_TRANSPORT_CONTROL_HELLO;
        ASSERT_EQ(xgl_transport_receive(&Transport(), instance, &hello),
                  XGL_OK);
    }
};

TEST_F(XglTransportTxContractTest, MissingRouteDoesNotConsumeTheOnlyPeer) {
    EXPECT_EQ(Send(3U), XGL_ERR_ROUTE_NOT_FOUND);
    EXPECT_EQ(hello_calls, 0U);
    EXPECT_EQ(data_calls, 0U);
    ExpectFreshScopeCanSend();
}

TEST_F(XglTransportTxContractTest, BusyHelloRollsBackOnlyTheNewPeer) {
    hello_result = XGL_ERR_BUSY;
    EXPECT_EQ(Send(), XGL_ERR_BUSY);
    EXPECT_EQ(hello_calls, 1U);
    EXPECT_EQ(data_calls, 0U);
    hello_result = XGL_OK;
    ExpectFreshScopeCanSend();
}

TEST_F(XglTransportTxContractTest, RecordAllocationFailureReleasesNewPeer) {
    resource.Replace(Transport().memory.tx_packet);
    EXPECT_EQ(Send(), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(hello_calls, 0U);
    EXPECT_EQ(data_calls, 0U);
    resource.reject = false;
    ExpectFreshScopeCanSend();
}

TEST_F(XglTransportTxContractTest, PayloadAllocationFailureReleasesNewPeer) {
    resource.Replace(Transport().memory.tx_payload);
    EXPECT_EQ(Send(), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(hello_calls, 0U);
    EXPECT_EQ(data_calls, 0U);
    resource.reject = false;
    ExpectFreshScopeCanSend();
}

TEST_F(XglTransportTxContractTest, RejectedFirstDataDoesNotRetainEmptyScope) {
    data_result = XGL_ERR_BUSY;
    EXPECT_EQ(Send(), XGL_ERR_BUSY);
    EXPECT_EQ(data_calls, 1U);
    data_result = XGL_OK;
    ExpectFreshScopeCanSend();
}

TEST_F(XglTransportTxContractTest, FailedSendPreservesPreviouslyExistingScope) {
    auto* peer = transport_get_or_create_peer_scope(&Transport(), 2U, 0U, 0U);
    ASSERT_NE(peer, nullptr);
    hello_result = XGL_ERR_BUSY;
    EXPECT_EQ(Send(), XGL_ERR_BUSY);
    EXPECT_EQ(Transport().peers, peer);
    hello_result = XGL_OK;
    EXPECT_EQ(Send(2U, 7U), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(Send(), XGL_OK);
}

TEST_F(XglTransportTxContractTest, FailedSendPreservesAcknowledgedHistory) {
    ASSERT_EQ(Send(), XGL_OK);
    Acknowledge(0U);
    auto* peer = Transport().peers;
    ASSERT_NE(peer, nullptr);
    ASSERT_TRUE(xgl_reliable_is_empty(&peer->reliable_queue));
    data_result = XGL_ERR_BUSY;
    EXPECT_EQ(Send(), XGL_ERR_BUSY);
    EXPECT_EQ(Transport().peers, peer);
    EXPECT_EQ(peer->tx_window.next_packet_number, 1U);
    data_result = XGL_OK;
    ASSERT_EQ(Send(), XGL_OK);
    EXPECT_EQ(last_data_number, 1U);
}

TEST_F(XglTransportTxContractTest, ReliableBroadcastFailsBeforePeerOrPhyUse) {
    ASSERT_EQ(xgl_route_table_remove(&instance->route_table, 2U), XGL_OK);
    ASSERT_EQ(xgl_route_table_add(&instance->route_table, XGL_BROADCAST_ID,
                                  &phy, 128U, 1000U, 0U),
              XGL_OK);
    EXPECT_EQ(Send(XGL_BROADCAST_ID), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(Transport().peers, nullptr);
    EXPECT_EQ(hello_calls, 0U);
    EXPECT_EQ(data_calls, 0U);
    EXPECT_EQ(Send(XGL_BROADCAST_ID, 0U, false), XGL_OK);
    EXPECT_EQ(data_calls, 1U);
}

TEST_F(XglTransportTxContractTest,
       GlobalCapacityRejectsBeforeAnnouncingNewScope) {
    Transport().max_peers = 2U;
    ASSERT_EQ(Send(2U, 6U), XGL_OK);
    EXPECT_EQ(Send(2U, 7U), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(hello_calls, 1U);
    EXPECT_EQ(data_calls, 1U);
    EXPECT_EQ(transport_find_peer_scope(&Transport(), 2U, 7U, 0U), nullptr);
    auto* existing = transport_find_peer_scope(&Transport(), 2U, 6U, 0U);
    ASSERT_NE(existing, nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&existing->reliable_queue), 1U);
}

TEST_F(XglTransportTxContractTest,
       FailedScopeErrorPrecedesOtherScopeCapacityPressure) {
    Transport().memory.peer = xgl_test_host_allocator();
    Transport().max_peers = 2U;
    auto* failed = transport_get_or_create_peer_scope(&Transport(), 2U, 7U, 0U);
    ASSERT_NE(failed, nullptr);
    transport_fail_peer(&Transport(), instance, failed, XGL_ERR_CANCELLED);
    ASSERT_EQ(Send(2U, 6U), XGL_OK);
    EXPECT_EQ(Send(2U, 7U), XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(transport_find_peer_scope(&Transport(), 2U, 7U, 0U), failed);
    EXPECT_TRUE(failed->failed);
    EXPECT_EQ(hello_calls, 1U);
    EXPECT_EQ(data_calls, 1U);
}

TEST_F(XglTransportTxContractTest,
       EmptyHelloExpiresWithIdleReclamationDisabled) {
    Hello(100U);
    ASSERT_NE(Transport().peers, nullptr);
    EXPECT_EQ(xgl_transport_run(&Transport(), instance, 5099U), XGL_OK);
    ASSERT_NE(Transport().peers, nullptr);
    EXPECT_EQ(xgl_transport_run(&Transport(), instance, 5100U), XGL_OK);
    EXPECT_EQ(Transport().peers, nullptr);
    EXPECT_EQ(Send(2U, 8U), XGL_OK);
}

TEST_F(XglTransportTxContractTest, EmptyHelloPublishesItsLeaseDeadline) {
    Hello(100U);
    uint32_t timeout = UINT32_MAX;
    EXPECT_TRUE(xgl_transport_next_timeout(&Transport(), 200U, &timeout));
    EXPECT_EQ(timeout, 4900U);
    EXPECT_TRUE(xgl_transport_next_timeout(&Transport(), 5100U, &timeout));
    EXPECT_EQ(timeout, 0U);
}

TEST_F(XglTransportTxContractTest,
       DuplicateHelloDoesNotExtendEmptyReservation) {
    Hello(100U);
    auto* reserved = Transport().peers;
    Hello(4900U);
    EXPECT_EQ(Transport().peers, reserved);
    EXPECT_EQ(xgl_transport_run(&Transport(), instance, 5100U), XGL_OK);
    EXPECT_EQ(Transport().peers, nullptr);
}

TEST_F(XglTransportTxContractTest, EmptyHelloLeaseHandlesClockWrap) {
    Hello(UINT32_MAX - 99U);
    EXPECT_EQ(xgl_transport_run(&Transport(), instance, 4899U), XGL_OK);
    ASSERT_NE(Transport().peers, nullptr);
    EXPECT_EQ(xgl_transport_run(&Transport(), instance, 4900U), XGL_OK);
    EXPECT_EQ(Transport().peers, nullptr);
}

TEST_F(XglTransportTxContractTest, HelloLeasePreservesAcceptedDataHistory) {
    Hello(100U, 0U);
    ASSERT_EQ(Send(), XGL_OK);
    Acknowledge(0U);
    auto* used = Transport().peers;
    EXPECT_EQ(xgl_transport_run(&Transport(), instance, 6000U), XGL_OK);
    EXPECT_EQ(Transport().peers, used);
    EXPECT_EQ(used->tx_window.next_packet_number, 1U);
}

TEST_F(XglTransportTxContractTest, HelloLeasePreservesFailedScopeIdentity) {
    Hello(100U);
    xgl_packet_t reset = {};
    reset.source_id = 2U;
    reset.target_id = 1U;
    reset.connection_id = 7U;
    reset.packet_type = XGL_PACKET_TYPE_CONTROL;
    reset.data_type = XGL_TRANSPORT_CONTROL_RESET;
    ASSERT_EQ(xgl_transport_receive(&Transport(), instance, &reset), XGL_OK);
    auto* failed = Transport().peers;
    ASSERT_TRUE(failed->failed);
    EXPECT_EQ(xgl_transport_run(&Transport(), instance, 6000U), XGL_OK);
    EXPECT_EQ(Transport().peers, failed);
}

} /* namespace */
