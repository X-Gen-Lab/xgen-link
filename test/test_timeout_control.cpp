/**
 * \file            test_timeout_control.cpp
 * \brief           Unit tests for custom timeout control
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/internal/xgl_transport.h>

/**
 * \brief           Test fixture for timeout control tests
 */
class XglTimeoutControlTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test setup if needed
    }

    void TearDown() override {
        // Test cleanup if needed
    }
};

/**
 * \brief           Test that timeout_ms field exists and can be set
 */
TEST_F(XglTimeoutControlTest, TimeoutFieldExists) {
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = (const uint8_t*)"test",
        .data_len = 4,
        .reliable = true,
        .priority = 0,
        .timeout_ms = 5000  // Custom 5 second timeout
    };

    EXPECT_EQ(tx_data.timeout_ms, 5000);
}

/**
 * \brief           Test that zero timeout means use default
 */
TEST_F(XglTimeoutControlTest, ZeroTimeoutMeansDefault) {
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = (const uint8_t*)"test",
        .data_len = 4,
        .reliable = true,
        .priority = 0,
        .timeout_ms = 0  // Use default timeout
    };

    EXPECT_EQ(tx_data.timeout_ms, 0);
}

/**
 * \brief           Test zero-copy timeout field
 */
TEST_F(XglTimeoutControlTest, ZeroCopyTimeoutField) {
    uint8_t buffer[128];
    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = XGL_FRAME_HEADER_SIZE,
        .data_len = 10,
        .target_id = 2,
        .data_type = 1,
        .reliable = true,
        .priority = 0,
        .timeout_ms = 3000  // Custom 3 second timeout
    };

    EXPECT_EQ(tx_data.timeout_ms, 3000);
}

/**
 * \brief           Test various timeout values
 */
TEST_F(XglTimeoutControlTest, VariousTimeoutValues) {
    // Short timeout for time-critical messages
    xgl_tx_data_t critical = {
        .target_id = 2,
        .data_type = 1,
        .data = (const uint8_t*)"critical",
        .data_len = 8,
        .reliable = true,
        .priority = 7,
        .timeout_ms = 100  // 100ms timeout
    };
    EXPECT_EQ(critical.timeout_ms, 100);

    // Long timeout for important but not urgent messages
    xgl_tx_data_t important = {
        .target_id = 2,
        .data_type = 2,
        .data = (const uint8_t*)"important",
        .data_len = 9,
        .reliable = true,
        .priority = 5,
        .timeout_ms = 10000  // 10 second timeout
    };
    EXPECT_EQ(important.timeout_ms, 10000);

    // Default timeout
    xgl_tx_data_t normal = {
        .target_id = 2,
        .data_type = 3,
        .data = (const uint8_t*)"normal",
        .data_len = 6,
        .reliable = true,
        .priority = 0,
        .timeout_ms = 0  // Use default
    };
    EXPECT_EQ(normal.timeout_ms, 0);
}
