/**
 * \file            test_error_properties.cpp
 * \brief           Error handling property tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "property_framework.h"
#include "../mocks/mock_allocator.h"
#include "../mocks/mock_phy.h"
#include <xgl/xgl.h>
#include <vector>
#include <string>
#include <set>

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AtLeast;

static uint8_t random_valid_source_id(PropertyTestGenerator& gen) {
    return static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
}

/*---------------------------------------------------------------------------*/
/* Mock Error Callback                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Mock error callback for testing
 */
class MockErrorCallback {
public:
    MOCK_METHOD(void, error_callback_impl, 
                (xgl_handle_t handle, xgl_error_t error, 
                 const char* message, void* user_data));
    
    /**
     * \brief           Get C-style callback function
     */
    static xgl_error_callback_t get_callback() {
        return [](xgl_handle_t handle, xgl_error_t error, 
                  const char* message, void* user_data) {
            MockErrorCallback* mock = static_cast<MockErrorCallback*>(user_data);
            mock->error_callback_impl(handle, error, message, user_data);
        };
    }
    
    /**
     * \brief           Track error invocations
     */
    struct ErrorInvocation {
        xgl_error_t error;
        std::string message;
    };
    
    std::vector<ErrorInvocation> invocations;
    
    void record_error(xgl_handle_t handle, xgl_error_t error, 
                     const char* message, void* user_data) {
        (void)handle;
        (void)user_data;
        invocations.push_back({error, message ? message : ""});
    }
};

/*---------------------------------------------------------------------------*/
/* Property 24: Error Code Specificity                                       */
/*---------------------------------------------------------------------------*/

/* Feature: x-gen-link, Property 24: Error Code Specificity */
TEST(XglErrorProperties, ErrorCodeSpecificity) {
    PropertyTestGenerator gen;
    
    /* Test various error conditions and verify specific error codes */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        
        /* Test 1: NULL pointer errors */
        {
            xgl_error_t err = xgl_init(nullptr);
            EXPECT_EQ(err, XGL_ERR_NULL_POINTER)
                << "Expected XGL_ERR_NULL_POINTER for NULL handle in iteration " 
                << iteration;
        }
        
        /* Test 2: Invalid parameter errors */
        {
            xgl_config_t config;
            xgl_config_get_default(&config);
            config.source_id = random_valid_source_id(gen);
            
            /* Invalid window size (0) */
            config.protocol.window_size = 0;
            xgl_error_t err = xgl_config_validate(&config);
            EXPECT_EQ(err, XGL_ERR_INVALID_PARAM)
                << "Expected XGL_ERR_INVALID_PARAM for invalid window size in iteration " 
                << iteration;
            
            /* Invalid max_frame_size (0) */
            config.protocol.window_size = 4;
            config.protocol.max_frame_size = 0;
            err = xgl_config_validate(&config);
            EXPECT_EQ(err, XGL_ERR_INVALID_PARAM)
                << "Expected XGL_ERR_INVALID_PARAM for invalid max_frame_size in iteration " 
                << iteration;
        }
        
        /* Test 3: Not initialized error */
        {
            xgl_config_t config;
            xgl_config_get_default(&config);
            config.source_id = random_valid_source_id(gen);
            
            /* Create instance but don't initialize */
            xgl_handle_t handle = xgl_create(&config);
            ASSERT_NE(handle, nullptr);
            
            /* Try to use uninitialized instance */
            xgl_statistics_t stats;
            xgl_error_t err = xgl_stats_get(handle, &stats);
            EXPECT_EQ(err, XGL_ERR_NOT_INITIALIZED)
                << "Expected XGL_ERR_NOT_INITIALIZED for uninitialized instance in iteration " 
                << iteration;
            
            xgl_destroy(handle);
        }
        
        /* Test 4: Route not found error */
        {
            /* Setup PHY */
            xgl_phy_ops_t phy = {
                .tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
                    (void)data; (void)len; (void)user_data;
                    return XGL_OK;
                },
                .rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
                    (void)buffer; (void)user_data;
                    *len = 0;
                    return XGL_ERR_TIMEOUT;
                },
                .user_data = nullptr
            };
            
            /* Setup route for target ID 2 only */
            xgl_route_item_t route = {
                .target_id = 2,
                .phy = &phy,
                .max_frame_size = 256,
                .read_freq_hz = 100,
                .metric = 0
            };
            
            xgl_config_t config;
            xgl_config_get_default(&config);
            config.source_id = 1;
            config.protocol.max_frame_size = 256;
            config.memory.rx_buffer_size = config.protocol.max_frame_size;
            config.route_table = &route;
            config.route_table_len = 1;
            
            xgl_handle_t handle = xgl_create(&config);
            ASSERT_NE(handle, nullptr);
            ASSERT_EQ(xgl_init(handle), XGL_OK);
            
            /* Try to send to non-existent route (target ID 99) */
            std::vector<uint8_t> test_data = gen.random_bytes(16);
            xgl_tx_data_t tx_data = {
                .target_id = 99,  /* No route for this ID */
                .data_type = gen.random_uint8(),
                .data = test_data.data(),
                .data_len = test_data.size(),
                .reliable = false,
                .priority = 0,
                .timeout_ms = 0
            };
            
            xgl_error_t err = xgl_send(handle, &tx_data);
            EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND)
                << "Expected XGL_ERR_ROUTE_NOT_FOUND for non-existent route in iteration " 
                << iteration;
            
            xgl_destroy(handle);
        }
        
        /* Test 5: Buffer too small error */
        {
            xgl_config_t config;
            xgl_config_get_default(&config);
            config.source_id = random_valid_source_id(gen);
            config.protocol.max_frame_size = 256;
            /* RX buffer too small (less than full max_frame_size) */
            config.memory.rx_buffer_size = 100;  /* Too small */
            
            xgl_error_t err = xgl_config_validate(&config);
            EXPECT_EQ(err, XGL_ERR_BUFFER_TOO_SMALL)
                << "Expected XGL_ERR_BUFFER_TOO_SMALL for insufficient RX buffer in iteration " 
                << iteration;
        }
    }
    
    /* Property: For any error condition, the protocol returns a specific error code */
    /* We've verified that different error conditions produce different, specific codes */
    
    /* Verify all error codes have unique values */
    std::set<int> error_codes = {
        XGL_OK,
        XGL_ERR_INVALID_PARAM,
        XGL_ERR_NULL_POINTER,
        XGL_ERR_NOT_INITIALIZED,
        XGL_ERR_ALREADY_INITIALIZED,
        XGL_ERR_NO_MEMORY,
        XGL_ERR_POOL_EXHAUSTED,
        XGL_ERR_BUFFER_TOO_SMALL,
        XGL_ERR_ROUTE_NOT_FOUND,
        XGL_ERR_TX_FAILED,
        XGL_ERR_TIMEOUT,
        XGL_ERR_ACK_TIMEOUT,
        XGL_ERR_INVALID_FRAME,
        XGL_ERR_CRC_FAILED,
        XGL_ERR_INVALID_VERSION,
        XGL_ERR_INVALID_DATA_TYPE,
        XGL_ERR_SEQUENCE_ERROR,
        XGL_ERR_BUSY,
        XGL_ERR_QUEUE_FULL,
        XGL_ERR_WINDOW_FULL
    };
    
    /* All error codes should be unique */
    EXPECT_EQ(error_codes.size(), 20)
        << "Error codes are not unique";
    
    /* Verify all error codes have string descriptions */
    for (int code : error_codes) {
        const char* str = xgl_error_string(static_cast<xgl_error_t>(code));
        EXPECT_NE(str, nullptr) << "Error code " << code << " has no string";
        EXPECT_STRNE(str, "") << "Error code " << code << " has empty string";
        EXPECT_STRNE(str, "Unknown error") 
            << "Error code " << code << " returns 'Unknown error'";
    }
}

/*---------------------------------------------------------------------------*/
/* Property 25: Error Callback Invocation                                    */
/*---------------------------------------------------------------------------*/

/* Feature: x-gen-link, Property 25: Error Callback Invocation */
TEST(XglErrorProperties, ErrorCallbackInvocation) {
    PropertyTestGenerator gen;
    
    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        MockErrorCallback mock_error_cb;
        
        /* Setup expectations: error callback should be invoked for errors */
        EXPECT_CALL(mock_error_cb, error_callback_impl(_, _, _, _))
            .WillRepeatedly(Invoke(&mock_error_cb, &MockErrorCallback::record_error));
        
        /* Setup PHY that always succeeds (to avoid complex failure scenarios) */
        xgl_phy_ops_t phy = {
            .tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
                (void)data; (void)len; (void)user_data;
                return XGL_OK;
            },
            .rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
                (void)buffer; (void)user_data;
                *len = 0;
                return XGL_ERR_TIMEOUT;
            },
            .user_data = nullptr
        };
        
        /* Setup route */
        xgl_route_item_t route = {
            .target_id = 2,
            .phy = &phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        };
        
        /* Create configuration with error callback */
        xgl_config_t config;
        xgl_config_get_default(&config);
        config.source_id = 1;
        config.protocol.max_frame_size = 256;
        config.memory.rx_buffer_size = config.protocol.max_frame_size;
        config.route_table = &route;
        config.route_table_len = 1;
        config.error_callback = MockErrorCallback::get_callback();
        config.callback_user_data = &mock_error_cb;
        
        /* Create and initialize instance */
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr) << "Failed to create instance in iteration " << iteration;
        
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to initialize instance in iteration " << iteration;
        
        /* Clear any initialization errors */
        mock_error_cb.invocations.clear();
        
        /* Test: Send to non-existent route (should invoke error callback) */
        {
            std::vector<uint8_t> test_data = gen.random_bytes(16);
            xgl_tx_data_t tx_data = {
                .target_id = 99,  /* No route for this ID */
                .data_type = gen.random_uint8(),
                .data = test_data.data(),
                .data_len = test_data.size(),
                .reliable = false,
                .priority = 0,
                .timeout_ms = 0
            };
            
            size_t invocations_before = mock_error_cb.invocations.size();
            xgl_error_t send_err = xgl_send(handle, &tx_data);
            
            /* Verify error was returned */
            EXPECT_EQ(send_err, XGL_ERR_ROUTE_NOT_FOUND)
                << "Expected XGL_ERR_ROUTE_NOT_FOUND in iteration " << iteration;
            
            /* Property: Error callback should be invoked with correct error code */
            EXPECT_GT(mock_error_cb.invocations.size(), invocations_before)
                << "Error callback not invoked for route not found in iteration " << iteration;
            
            if (mock_error_cb.invocations.size() > invocations_before) {
                auto& last_error = mock_error_cb.invocations.back();
                EXPECT_EQ(last_error.error, XGL_ERR_ROUTE_NOT_FOUND)
                    << "Error callback invoked with wrong error code in iteration " << iteration;
                EXPECT_FALSE(last_error.message.empty())
                    << "Error callback invoked with empty message in iteration " << iteration;
            }
        }
        
        /* Verify all error invocations had valid error codes and messages */
        for (const auto& inv : mock_error_cb.invocations) {
            EXPECT_NE(inv.error, XGL_OK)
                << "Error callback invoked with XGL_OK in iteration " << iteration;
            EXPECT_FALSE(inv.message.empty())
                << "Error callback invoked with empty message in iteration " << iteration;
            
            /* Verify error code has a valid string description */
            const char* err_str = xgl_error_string(inv.error);
            EXPECT_NE(err_str, nullptr);
            EXPECT_STRNE(err_str, "");
        }
        
        /* Clean up */
        xgl_destroy(handle);
        
        testing::Mock::VerifyAndClearExpectations(&mock_error_cb);
    }
}

/*---------------------------------------------------------------------------*/
/* Property 26: Error Statistics                                             */
/*---------------------------------------------------------------------------*/

/* Feature: x-gen-link, Property 26: Error Statistics */
TEST(XglErrorProperties, ErrorStatistics) {
    PropertyTestGenerator gen;
    
    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        
        /* Setup PHY that can fail transmission */
        int tx_fail_count = 0;
        int tx_fail_every_n = 3 + (gen.random_uint8() % 5);  /* Fail every N transmissions */
        
        xgl_phy_ops_t phy = {
            .tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
                (void)data; (void)len;
                int* fail_info = static_cast<int*>(user_data);
                int& count = fail_info[0];
                int fail_every = fail_info[1];
                
                count++;
                if (count % fail_every == 0) {
                    return XGL_ERR_TX_FAILED;  /* Simulate transmission failure */
                }
                return XGL_OK;
            },
            .rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
                (void)buffer; (void)user_data;
                *len = 0;
                return XGL_ERR_TIMEOUT;
            },
            .user_data = nullptr
        };
        
        int fail_info[2] = {tx_fail_count, tx_fail_every_n};
        phy.user_data = fail_info;
        
        /* Setup route */
        xgl_route_item_t route = {
            .target_id = 2,
            .phy = &phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        };
        
        /* Create configuration */
        xgl_config_t config;
        xgl_config_get_default(&config);
        config.source_id = 1;
        config.protocol.max_frame_size = 256;
        config.memory.rx_buffer_size = config.protocol.max_frame_size;
        config.route_table = &route;
        config.route_table_len = 1;
        
        /* Create and initialize instance */
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr) << "Failed to create instance in iteration " << iteration;
        
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to initialize instance in iteration " << iteration;
        
        /* Get initial statistics */
        xgl_statistics_t stats_before;
        err = xgl_stats_get(handle, &stats_before);
        ASSERT_EQ(err, XGL_OK) << "Failed to get initial statistics in iteration " << iteration;
        
        /* Perform multiple send operations (some will fail) */
        int successful_sends = 0;
        int failed_sends = 0;
        int route_not_found_errors = 0;
        
        std::vector<uint8_t> test_data = gen.random_bytes(32);
        
        for (int i = 0; i < 20; ++i) {
            /* Randomly send to valid or invalid route */
            uint8_t target_id = (gen.random_uint8() % 10 < 8) ? 2 : 99;  /* 80% valid, 20% invalid */
            
            xgl_tx_data_t tx_data = {
                .target_id = target_id,
                .data_type = gen.random_uint8(),
                .data = test_data.data(),
                .data_len = test_data.size(),
                .reliable = false,
                .priority = static_cast<uint8_t>(gen.random_uint8() % 8),
                .timeout_ms = 0
            };
            
            xgl_error_t send_err = xgl_send(handle, &tx_data);
            
            if (send_err == XGL_OK) {
                successful_sends++;
            } else if (send_err == XGL_ERR_TX_FAILED) {
                failed_sends++;
            } else if (send_err == XGL_ERR_ROUTE_NOT_FOUND) {
                route_not_found_errors++;
            }
        }
        
        /* Get statistics after operations */
        xgl_statistics_t stats_after;
        err = xgl_stats_get(handle, &stats_after);
        ASSERT_EQ(err, XGL_OK) << "Failed to get statistics after operations in iteration " 
                               << iteration;
        
        /* Property: Error statistics should be incremented for each error occurrence */
        
        /* Verify TX error counter increased (check datalink and transport layers) */
        /* Note: Network layer is bypassed in current implementation, so we don't check it */
        uint64_t tx_errors_delta = (stats_after.datalink.tx_errors - stats_before.datalink.tx_errors) +
                                    (stats_after.transport.tx_errors - stats_before.transport.tx_errors);
        EXPECT_GE(tx_errors_delta, static_cast<uint64_t>(failed_sends + route_not_found_errors))
            << "TX error counter not incremented correctly in iteration " << iteration
            << " (expected at least " << (failed_sends + route_not_found_errors) 
            << " errors, got " << tx_errors_delta << ")"
            << " [datalink: " << (stats_after.datalink.tx_errors - stats_before.datalink.tx_errors)
            << ", transport: " << (stats_after.transport.tx_errors - stats_before.transport.tx_errors) << "]";
        
        /* Verify TX packet counter increased (may count at multiple layers, so just check it increased) */
        uint64_t tx_packets_delta = stats_after.datalink.tx_packets - stats_before.datalink.tx_packets;
        if (successful_sends > 0) {
            EXPECT_GT(tx_packets_delta, 0)
                << "TX packet counter did not increase despite successful sends in iteration " 
                << iteration;
        }
        
        /* Verify statistics are consistent (monotonically increasing) */
        EXPECT_GE(stats_after.datalink.tx_packets, stats_before.datalink.tx_packets)
            << "TX packet count decreased in iteration " << iteration;
        EXPECT_GE(stats_after.datalink.tx_errors, stats_before.datalink.tx_errors)
            << "TX error count decreased in iteration " << iteration;
        EXPECT_GE(stats_after.datalink.tx_bytes, stats_before.datalink.tx_bytes)
            << "TX byte count decreased in iteration " << iteration;
        
        /* Test statistics reset */
        err = xgl_stats_reset(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to reset statistics in iteration " << iteration;
        
        /* Get statistics after reset */
        xgl_statistics_t stats_reset;
        err = xgl_stats_get(handle, &stats_reset);
        ASSERT_EQ(err, XGL_OK) << "Failed to get statistics after reset in iteration " 
                               << iteration;
        
        /* Verify all counters were reset to zero */
        EXPECT_EQ(stats_reset.datalink.tx_packets, 0)
            << "TX packets not reset in iteration " << iteration;
        EXPECT_EQ(stats_reset.datalink.tx_bytes, 0)
            << "TX bytes not reset in iteration " << iteration;
        EXPECT_EQ(stats_reset.datalink.tx_errors, 0)
            << "TX errors not reset in iteration " << iteration;
        EXPECT_EQ(stats_reset.datalink.rx_packets, 0)
            << "RX packets not reset in iteration " << iteration;
        EXPECT_EQ(stats_reset.datalink.rx_bytes, 0)
            << "RX bytes not reset in iteration " << iteration;
        EXPECT_EQ(stats_reset.datalink.rx_errors, 0)
            << "RX errors not reset in iteration " << iteration;
        
        /* Perform one more operation after reset to verify statistics still work */
        xgl_tx_data_t tx_data_after_reset = {
            .target_id = 2,
            .data_type = gen.random_uint8(),
            .data = test_data.data(),
            .data_len = test_data.size(),
            .reliable = false,
            .priority = 0,
            .timeout_ms = 0
        };
        
        xgl_send(handle, &tx_data_after_reset);
        
        xgl_statistics_t stats_final;
        err = xgl_stats_get(handle, &stats_final);
        ASSERT_EQ(err, XGL_OK);
        
        /* Verify statistics are being tracked after reset */
        EXPECT_TRUE(stats_final.datalink.tx_packets > 0 || stats_final.datalink.tx_errors > 0)
            << "Statistics not tracking after reset in iteration " << iteration;
        
        /* Clean up */
        xgl_destroy(handle);
    }
}

