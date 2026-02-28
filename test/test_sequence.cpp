/**
 * \file            test_sequence.cpp
 * \brief           Sequence number management unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_sequence.h>
#include <xgl/xgl_error.h>

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglSequenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Initialize sequence context */
        xgl_error_t err = xgl_sequence_init(&seq_ctx, MAX_ROUTES, nullptr);
        ASSERT_EQ(err, XGL_OK);
    }
    
    void TearDown() override {
        xgl_sequence_destroy(&seq_ctx);
    }
    
    static constexpr size_t MAX_ROUTES = 256;
    static constexpr uint8_t TARGET_ID_1 = 1;
    static constexpr uint8_t TARGET_ID_2 = 2;
    static constexpr uint8_t TARGET_ID_3 = 3;
    
    xgl_sequence_ctx_t seq_ctx;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, InitializeSequenceContext) {
    xgl_sequence_ctx_t ctx;
    
    xgl_error_t err = xgl_sequence_init(&ctx, 256, nullptr);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_NE(ctx.seq_numbers, nullptr);
    EXPECT_EQ(ctx.max_routes, 256);
    
    xgl_sequence_destroy(&ctx);
}

TEST_F(XglSequenceTest, InitializeWithNullContext) {
    xgl_error_t err = xgl_sequence_init(nullptr, 256, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglSequenceTest, InitializeWithZeroRoutes) {
    xgl_sequence_ctx_t ctx;
    
    xgl_error_t err = xgl_sequence_init(&ctx, 0, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglSequenceTest, InitializeWithTooManyRoutes) {
    xgl_sequence_ctx_t ctx;
    
    xgl_error_t err = xgl_sequence_init(&ctx, XGL_SEQ_MAX_ROUTES + 1, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglSequenceTest, InitialSequenceNumberIsZero) {
    uint8_t seq_num;
    
    xgl_error_t err = xgl_sequence_get_current(&seq_ctx, TARGET_ID_1, &seq_num);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(seq_num, XGL_SEQ_NUM_INITIAL);
}

/*---------------------------------------------------------------------------*/
/* Get Next Sequence Number Tests                                            */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, GetNextSequenceNumber) {
    uint8_t seq_num;
    
    xgl_error_t err = xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(seq_num, 0);
}

TEST_F(XglSequenceTest, GetNextIncrementsSequenceNumber) {
    uint8_t seq_num1, seq_num2;
    
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num1);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num2);
    
    EXPECT_EQ(seq_num1, 0);
    EXPECT_EQ(seq_num2, 1);
}

TEST_F(XglSequenceTest, GetNextWithNullContext) {
    uint8_t seq_num;
    
    xgl_error_t err = xgl_sequence_get_next(nullptr, TARGET_ID_1, &seq_num);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglSequenceTest, GetNextWithNullOutput) {
    xgl_error_t err = xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglSequenceTest, GetNextWithInvalidTargetId) {
    uint8_t seq_num;
    
    xgl_error_t err = xgl_sequence_get_next(&seq_ctx, 255, &seq_num);
    
    EXPECT_EQ(err, XGL_OK);  /* 255 is valid for 256 routes */
    
    /* Try with out of range */
    xgl_sequence_ctx_t small_ctx;
    xgl_sequence_init(&small_ctx, 10, nullptr);
    
    err = xgl_sequence_get_next(&small_ctx, 10, &seq_num);
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
    
    xgl_sequence_destroy(&small_ctx);
}

/*---------------------------------------------------------------------------*/
/* Per-Route Tracking Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, IndependentSequenceNumbersPerRoute) {
    uint8_t seq1, seq2, seq3;
    
    /* Get sequence numbers for different routes */
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq1);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_2, &seq2);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_3, &seq3);
    
    /* All should start at 0 */
    EXPECT_EQ(seq1, 0);
    EXPECT_EQ(seq2, 0);
    EXPECT_EQ(seq3, 0);
    
    /* Get next for route 1 */
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq1);
    EXPECT_EQ(seq1, 1);
    
    /* Route 2 and 3 should still be at 1 (next would be 1) */
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_2, &seq2);
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_3, &seq3);
    EXPECT_EQ(seq2, 1);
    EXPECT_EQ(seq3, 1);
}

/*---------------------------------------------------------------------------*/
/* Wraparound Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, SequenceNumberWrapsAround) {
    uint8_t seq_num;
    
    /* Set sequence number to 255 */
    for (int i = 0; i < 255; i++) {
        xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num);
    }
    
    EXPECT_EQ(seq_num, 254);
    
    /* Next should be 255 */
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num);
    EXPECT_EQ(seq_num, 255);
    
    /* Next should wrap to 0 */
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num);
    EXPECT_EQ(seq_num, 0);
}

TEST_F(XglSequenceTest, IncrementHandlesWraparound) {
    EXPECT_EQ(xgl_sequence_increment(0), 1);
    EXPECT_EQ(xgl_sequence_increment(254), 255);
    EXPECT_EQ(xgl_sequence_increment(255), 0);
}

/*---------------------------------------------------------------------------*/
/* Get Current Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, GetCurrentDoesNotIncrement) {
    uint8_t seq1, seq2;
    
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_1, &seq1);
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_1, &seq2);
    
    EXPECT_EQ(seq1, seq2);
    EXPECT_EQ(seq1, 0);
}

TEST_F(XglSequenceTest, GetCurrentAfterGetNext) {
    uint8_t seq_next, seq_current;
    
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_next);
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_1, &seq_current);
    
    EXPECT_EQ(seq_next, 0);
    EXPECT_EQ(seq_current, 1);  /* Current is now 1 after increment */
}

/*---------------------------------------------------------------------------*/
/* Reset Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, ResetSequenceNumber) {
    uint8_t seq_num;
    
    /* Increment sequence number */
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq_num);
    
    /* Reset */
    xgl_error_t err = xgl_sequence_reset(&seq_ctx, TARGET_ID_1);
    EXPECT_EQ(err, XGL_OK);
    
    /* Should be back to initial value */
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_1, &seq_num);
    EXPECT_EQ(seq_num, XGL_SEQ_NUM_INITIAL);
}

TEST_F(XglSequenceTest, ResetDoesNotAffectOtherRoutes) {
    uint8_t seq1, seq2;
    
    /* Increment both routes */
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq1);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_2, &seq2);
    
    /* Reset route 1 */
    xgl_sequence_reset(&seq_ctx, TARGET_ID_1);
    
    /* Route 1 should be reset */
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_1, &seq1);
    EXPECT_EQ(seq1, 0);
    
    /* Route 2 should be unchanged */
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_2, &seq2);
    EXPECT_EQ(seq2, 1);
}

TEST_F(XglSequenceTest, ResetAllSequenceNumbers) {
    uint8_t seq1, seq2, seq3;
    
    /* Increment all routes */
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_1, &seq1);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_2, &seq2);
    xgl_sequence_get_next(&seq_ctx, TARGET_ID_3, &seq3);
    
    /* Reset all */
    xgl_sequence_reset_all(&seq_ctx);
    
    /* All should be reset */
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_1, &seq1);
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_2, &seq2);
    xgl_sequence_get_current(&seq_ctx, TARGET_ID_3, &seq3);
    
    EXPECT_EQ(seq1, 0);
    EXPECT_EQ(seq2, 0);
    EXPECT_EQ(seq3, 0);
}

/*---------------------------------------------------------------------------*/
/* Sequence Validation Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, ValidateSequenceNumberInWindow) {
    uint8_t expected = 10;
    uint8_t window_size = 8;
    
    /* Within forward window */
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 10, window_size));
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 11, window_size));
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 17, window_size));
    
    /* Within backward window */
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 9, window_size));
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 3, window_size));
}

TEST_F(XglSequenceTest, ValidateSequenceNumberOutOfWindow) {
    uint8_t expected = 10;
    uint8_t window_size = 8;
    
    /* Too far ahead */
    EXPECT_FALSE(xgl_sequence_is_valid(expected, 19, window_size));
    EXPECT_FALSE(xgl_sequence_is_valid(expected, 20, window_size));
    
    /* Too far behind */
    EXPECT_FALSE(xgl_sequence_is_valid(expected, 1, window_size));
    EXPECT_FALSE(xgl_sequence_is_valid(expected, 0, window_size));
}

TEST_F(XglSequenceTest, ValidateSequenceNumberWithWraparound) {
    uint8_t expected = 250;
    uint8_t window_size = 8;
    
    /* Forward window wraps around */
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 250, window_size));
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 255, window_size));
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 0, window_size));
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 1, window_size));
    
    /* Backward window */
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 249, window_size));
    EXPECT_TRUE(xgl_sequence_is_valid(expected, 243, window_size));
}

/*---------------------------------------------------------------------------*/
/* Sequence Difference Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, SequenceDifferenceWithoutWraparound) {
    EXPECT_EQ(xgl_sequence_diff(10, 5), 5);
    EXPECT_EQ(xgl_sequence_diff(5, 10), -5);
    EXPECT_EQ(xgl_sequence_diff(100, 100), 0);
}

TEST_F(XglSequenceTest, SequenceDifferenceWithWraparound) {
    /* 5 - 250 = 11 (with wraparound) */
    EXPECT_EQ(xgl_sequence_diff(5, 250), 11);
    
    /* 250 - 5 = -11 (with wraparound) */
    EXPECT_EQ(xgl_sequence_diff(250, 5), -11);
    
    /* 0 - 255 = 1 (with wraparound) */
    EXPECT_EQ(xgl_sequence_diff(0, 255), 1);
    
    /* 255 - 0 = -1 (with wraparound) */
    EXPECT_EQ(xgl_sequence_diff(255, 0), -1);
}

/*---------------------------------------------------------------------------*/
/* Sequence Comparison Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglSequenceTest, CompareSequenceNumbersWithoutWraparound) {
    EXPECT_LT(xgl_sequence_compare(5, 10), 0);
    EXPECT_GT(xgl_sequence_compare(10, 5), 0);
    EXPECT_EQ(xgl_sequence_compare(10, 10), 0);
}

TEST_F(XglSequenceTest, CompareSequenceNumbersWithWraparound) {
    /* 250 < 5 (with wraparound) */
    EXPECT_LT(xgl_sequence_compare(250, 5), 0);
    
    /* 5 > 250 (with wraparound) */
    EXPECT_GT(xgl_sequence_compare(5, 250), 0);
    
    /* 255 < 0 (with wraparound) */
    EXPECT_LT(xgl_sequence_compare(255, 0), 0);
    
    /* 0 > 255 (with wraparound) */
    EXPECT_GT(xgl_sequence_compare(0, 255), 0);
}

