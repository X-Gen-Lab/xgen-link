/**
 * \file            test_window.cpp
 * \brief           Unit tests for sliding window implementation
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include "xgl/xgl_window.h"

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglWindowTest : public ::testing::Test {
protected:
    xgl_sliding_window_t window;
    
    void SetUp() override {
        memset(&window, 0, sizeof(window));
    }
    
    void TearDown() override {
        xgl_window_destroy(&window);
    }
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglWindowTest, InitSuccess) {
    xgl_error_t err = xgl_window_init(&window, 8);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(window.window_size, 8);
    EXPECT_EQ(window.send_base, 0);
    EXPECT_EQ(window.next_seq_num, 0);
    EXPECT_EQ(window.expected_seq_num, 0);
    EXPECT_NE(window.ack_received, nullptr);
}

TEST_F(XglWindowTest, InitNullPointer) {
    xgl_error_t err = xgl_window_init(nullptr, 8);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglWindowTest, InitZeroWindowSize) {
    xgl_error_t err = xgl_window_init(&window, 0);
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglWindowTest, InitTooLargeWindowSize) {
    xgl_error_t err = xgl_window_init(&window, 129);
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglWindowTest, DestroyNullPointer) {
    /* Should not crash */
    xgl_window_destroy(nullptr);
}

/*---------------------------------------------------------------------------*/
/* Window State Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST_F(XglWindowTest, CanSendInitially) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);
    EXPECT_TRUE(xgl_window_can_send(&window));
}

TEST_F(XglWindowTest, CannotSendWhenWindowFull) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);
    
    /* Fill window */
    for (uint8_t i = 0; i < 4; i++) {
        EXPECT_TRUE(xgl_window_can_send(&window));
        xgl_window_advance_next_seq(&window);
    }
    
    /* Window should be full now */
    EXPECT_FALSE(xgl_window_can_send(&window));
}

TEST_F(XglWindowTest, GetNextSeq) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);
    
    EXPECT_EQ(xgl_window_get_next_seq(&window), 0);
    xgl_window_advance_next_seq(&window);
    EXPECT_EQ(xgl_window_get_next_seq(&window), 1);
    xgl_window_advance_next_seq(&window);
    EXPECT_EQ(xgl_window_get_next_seq(&window), 2);
}

TEST_F(XglWindowTest, GetUsage) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);
    
    EXPECT_EQ(xgl_window_get_usage(&window), 0);
    
    xgl_window_advance_next_seq(&window);
    EXPECT_EQ(xgl_window_get_usage(&window), 1);
    
    xgl_window_advance_next_seq(&window);
    EXPECT_EQ(xgl_window_get_usage(&window), 2);
}

/*---------------------------------------------------------------------------*/
/* ACK Handling Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST_F(XglWindowTest, MarkAckSuccess) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);
    
    /* Send some packets */
    xgl_window_advance_next_seq(&window);
    xgl_window_advance_next_seq(&window);
    xgl_window_advance_next_seq(&window);
    
    /* Mark ACK for seq 0 */
    xgl_error_t err = xgl_window_mark_ack(&window, 0);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_TRUE(xgl_window_is_acked(&window, 0));
}

TEST_F(XglWindowTest, MarkAckOutOfWindow) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);
    
    /* Try to mark ACK for seq number outside window */
    xgl_error_t err = xgl_window_mark_ack(&window, 10);
    EXPECT_EQ(err, XGL_ERR_SEQUENCE_ERROR);
}

TEST_F(XglWindowTest, AdvanceBaseOnConsecutiveAcks) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);
    
    /* Send 5 packets */
    for (uint8_t i = 0; i < 5; i++) {
        xgl_window_advance_next_seq(&window);
    }
    
    /* Mark ACKs for seq 0, 1, 2 */
    ASSERT_EQ(xgl_window_mark_ack(&window, 0), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack(&window, 1), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack(&window, 2), XGL_OK);
    
    /* Advance base */
    uint8_t advanced = xgl_window_advance_base(&window);
    EXPECT_EQ(advanced, 3);
    EXPECT_EQ(window.send_base, 3);
}

TEST_F(XglWindowTest, AdvanceBaseStopsAtGap) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);
    
    /* Send 5 packets */
    for (uint8_t i = 0; i < 5; i++) {
        xgl_window_advance_next_seq(&window);
    }
    
    /* Mark ACKs for seq 0, 2 (skip 1) */
    ASSERT_EQ(xgl_window_mark_ack(&window, 0), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack(&window, 2), XGL_OK);
    
    /* Advance base - should stop at seq 1 */
    uint8_t advanced = xgl_window_advance_base(&window);
    EXPECT_EQ(advanced, 1);
    EXPECT_EQ(window.send_base, 1);
    
    /* Now mark ACK for seq 1 */
    ASSERT_EQ(xgl_window_mark_ack(&window, 1), XGL_OK);
    
    /* Advance base again - should advance to seq 3 */
    advanced = xgl_window_advance_base(&window);
    EXPECT_EQ(advanced, 2);
    EXPECT_EQ(window.send_base, 3);
}

/*---------------------------------------------------------------------------*/
/* Window Boundary Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(XglWindowTest, IsInWindow) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);
    
    /* Send 2 packets */
    xgl_window_advance_next_seq(&window);
    xgl_window_advance_next_seq(&window);
    
    /* Check which sequence numbers are in window */
    EXPECT_TRUE(xgl_window_is_in_window(&window, 0));
    EXPECT_TRUE(xgl_window_is_in_window(&window, 1));
    EXPECT_TRUE(xgl_window_is_in_window(&window, 2));
    EXPECT_TRUE(xgl_window_is_in_window(&window, 3));
    EXPECT_FALSE(xgl_window_is_in_window(&window, 4));
    EXPECT_FALSE(xgl_window_is_in_window(&window, 5));
}

TEST_F(XglWindowTest, SequenceNumberWraparound) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);
    
    /* Set window near wraparound */
    window.send_base = 254;
    window.next_seq_num = 254;
    
    /* Send packets across wraparound */
    xgl_window_advance_next_seq(&window);  /* 255 */
    xgl_window_advance_next_seq(&window);  /* 0 */
    xgl_window_advance_next_seq(&window);  /* 1 */
    
    EXPECT_EQ(window.next_seq_num, 1);
    
    /* Check window boundaries */
    EXPECT_TRUE(xgl_window_is_in_window(&window, 254));
    EXPECT_TRUE(xgl_window_is_in_window(&window, 255));
    EXPECT_TRUE(xgl_window_is_in_window(&window, 0));
    EXPECT_TRUE(xgl_window_is_in_window(&window, 1));
    EXPECT_FALSE(xgl_window_is_in_window(&window, 2));
}

/*---------------------------------------------------------------------------*/
/* Reset Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(XglWindowTest, Reset) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);
    
    /* Send some packets and mark ACKs */
    for (uint8_t i = 0; i < 5; i++) {
        xgl_window_advance_next_seq(&window);
    }
    xgl_window_mark_ack(&window, 0);
    xgl_window_mark_ack(&window, 1);
    xgl_window_advance_base(&window);
    
    /* Reset window */
    xgl_window_reset(&window);
    
    /* Check state is reset */
    EXPECT_EQ(window.send_base, 0);
    EXPECT_EQ(window.next_seq_num, 0);
    EXPECT_EQ(window.expected_seq_num, 0);
    EXPECT_FALSE(xgl_window_is_acked(&window, 0));
    EXPECT_FALSE(xgl_window_is_acked(&window, 1));
}

/*---------------------------------------------------------------------------*/
/* Edge Cases                                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglWindowTest, MaxWindowSize) {
    xgl_error_t err = xgl_window_init(&window, 128);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(window.window_size, 128);
}

TEST_F(XglWindowTest, MinWindowSize) {
    xgl_error_t err = xgl_window_init(&window, 1);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(window.window_size, 1);
    
    /* Should be able to send one packet */
    EXPECT_TRUE(xgl_window_can_send(&window));
    xgl_window_advance_next_seq(&window);
    
    /* Window should be full */
    EXPECT_FALSE(xgl_window_can_send(&window));
}

TEST_F(XglWindowTest, FullWindowCycle) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);
    
    /* Fill window */
    for (uint8_t i = 0; i < 4; i++) {
        EXPECT_TRUE(xgl_window_can_send(&window));
        xgl_window_advance_next_seq(&window);
    }
    EXPECT_FALSE(xgl_window_can_send(&window));
    
    /* ACK all packets */
    for (uint8_t i = 0; i < 4; i++) {
        ASSERT_EQ(xgl_window_mark_ack(&window, i), XGL_OK);
    }
    
    /* Advance base */
    uint8_t advanced = xgl_window_advance_base(&window);
    EXPECT_EQ(advanced, 4);
    
    /* Should be able to send again */
    EXPECT_TRUE(xgl_window_can_send(&window));
}

/*---------------------------------------------------------------------------*/
/* Null Pointer Safety Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglWindowTest, CanSendNullPointer) {
    EXPECT_FALSE(xgl_window_can_send(nullptr));
}

TEST_F(XglWindowTest, GetNextSeqNullPointer) {
    EXPECT_EQ(xgl_window_get_next_seq(nullptr), 0);
}

TEST_F(XglWindowTest, AdvanceNextSeqNullPointer) {
    /* Should not crash */
    xgl_window_advance_next_seq(nullptr);
}

TEST_F(XglWindowTest, MarkAckNullPointer) {
    xgl_error_t err = xgl_window_mark_ack(nullptr, 0);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglWindowTest, AdvanceBaseNullPointer) {
    EXPECT_EQ(xgl_window_advance_base(nullptr), 0);
}

TEST_F(XglWindowTest, IsInWindowNullPointer) {
    EXPECT_FALSE(xgl_window_is_in_window(nullptr, 0));
}

TEST_F(XglWindowTest, GetUsageNullPointer) {
    EXPECT_EQ(xgl_window_get_usage(nullptr), 0);
}

TEST_F(XglWindowTest, ResetNullPointer) {
    /* Should not crash */
    xgl_window_reset(nullptr);
}

TEST_F(XglWindowTest, IsAckedNullPointer) {
    EXPECT_FALSE(xgl_window_is_acked(nullptr, 0));
}
