/**
 * \file            test_fragment_properties.cpp
 * \brief           Production FRAGMENT_EXT property tests
 */

#include <transport/xgl_fragment.h>
#include <transport/xgl_transport.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl_error.h>

#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#include <xgen/memory/libc_allocator.h>

#include "property_framework.h"

TEST(XglFragmentProperties, ManagerInitialization) {
    xgl_fragment_manager_t manager = {};
    xgl_error_t err =
        xgl_fragment_init(&manager, 10, 5000, xgm_allocator_libc());
    ASSERT_EQ(err, XGL_OK);
    EXPECT_EQ(manager.current_reassembly_bytes, 0U);

    xgl_fragment_destroy(&manager);
}

TEST(XglFragmentProperties, ManagerInitInvalidParameters) {
    EXPECT_EQ(xgl_fragment_init(nullptr, 10, 5000, xgm_allocator_libc()),
              XGL_ERR_NULL_POINTER);
}

TEST(XglFragmentProperties, MessageIdAssignmentIsMonotonic32Bit) {
    std::vector<uint32_t> message_ids;
    xgl_packet_interface_t lower = {};
    lower.ctx = &message_ids;
    lower.send = [](void* user, xgl_handle_t, xgl_packet_t* packet) {
        xgl_wire_ext_cursor_t cursor = {};
        xgl_error_t err = xgl_wire_ext_cursor_init(&cursor, packet->extensions,
                                                   packet->extensions_len);
        if (err != XGL_OK) {
            return err;
        }
        xgl_wire_ext_t ext = {};
        while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
            if (ext.type == XGL_WIRE_EXT_FRAGMENT) {
                uint32_t message_id = 0, offset = 0, total = 0;
                err = xgl_wire_decode_fragment_ext_value(
                    ext.value, ext.len, &message_id, &offset, &total);
                if (err == XGL_OK && offset == 0) {
                    static_cast<std::vector<uint32_t>*>(user)->push_back(
                        message_id);
                }
                return err;
            }
        }
        return XGL_ERR_INVALID_FRAME;
    };
    xgl_layer_stats_t stats = {};
    xgl_transport_config_t config = {};
    config.allocator = xgm_allocator_libc();
    config.local_id = 1;
    config.max_peers = 1;
    config.max_tx_packets = 1;
    config.window_size = 1;
    config.default_timeout_ms = 100;
    config.max_frame_size = 64;
    config.enable_fragmentation = true;
    config.max_message_size = 80;
    config.max_reassembly_slots = 1;
    config.max_reassembly_bytes = 80;
    config.max_tx_message_bytes = 80;
    config.lower_layer = &lower;
    config.stats = &stats;
    xgl_transport_ctx_t ctx = {};
    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);
    const uint8_t payload[80] = {};
    xgl_tx_data_t tx = {};
    tx.target_id = 2;
    tx.data = payload;
    tx.data_len = sizeof(payload);
    for (uint32_t i = 0; i < 1024U; ++i) {
        EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx), XGL_OK);
    }
    xgl_transport_destroy(&ctx);
    ASSERT_EQ(message_ids.size(), 1024U);
    for (uint32_t i = 0; i < message_ids.size(); ++i) {
        EXPECT_EQ(message_ids[i], i);
    }
}

TEST(XglFragmentProperties, FragmentExtensionReassemblyRoundTrip) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        xgl_fragment_manager_t manager = {};
        ASSERT_EQ(xgl_fragment_init(&manager, 10, 5000, xgm_allocator_libc()),
                  XGL_OK);

        size_t total_len = 2U + (gen.random_uint32() % 128U);
        std::vector<uint8_t> data = gen.random_bytes(total_len);
        size_t split = 1U + (gen.random_uint32() % (total_len - 1U));

        xgl_fragment_message_t complete = {};
        uint32_t message_id = static_cast<uint32_t>(iteration);

        ASSERT_EQ(xgl_fragment_process_ext(
                      &manager, 0x1234, 0xABCDEF01U, 0x01020304U, 7, message_id,
                      static_cast<uint32_t>(split),
                      static_cast<uint32_t>(total_len), data.data() + split,
                      total_len - split, &complete, 1000),
                  XGL_ERR_BUSY);

        ASSERT_EQ(xgl_fragment_process_ext(&manager, 0x1234, 0xABCDEF01U,
                                           0x01020304U, 7, message_id, 0,
                                           static_cast<uint32_t>(total_len),
                                           data.data(), split, &complete, 1001),
                  XGL_OK);

        ASSERT_NE(complete.data, nullptr);
        ASSERT_EQ(complete.len, total_len);
        EXPECT_EQ(memcmp(complete.data, data.data(), total_len), 0);
        EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);

        xgl_fragment_release_message(&manager, &complete);
        xgl_fragment_destroy(&manager);
    }
}
