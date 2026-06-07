/**
 * \file            test_ack.cpp
 * \brief           Unit tests for ACK/NACK handling
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_ack.h>
#include <xgl/xgl_types.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_wire.h>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglAckTest : public ::testing::Test {
protected:
    xgl_ack_handler_t handler;
    
    void SetUp() override {
        /* Initialize ACK handler */
        xgl_error_t err = xgl_ack_init(&handler, nullptr);
        ASSERT_EQ(err, XGL_OK);
    }
    
    void TearDown() override {
        /* Destroy ACK handler */
        xgl_ack_destroy(&handler);
    }
};

/*---------------------------------------------------------------------------*/
/* ACK Generation Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglAckTest, GenerateAckPacket) {
    uint8_t ack_buffer[64];
    size_t ack_len = 0;
    
    /* Generate ACK for sequence number 42 */
    xgl_error_t err = xgl_ack_generate(
        42,         /* seq_num */
        0x01,       /* source_id (original sender) */
        0x02,       /* target_id (original receiver) */
        ack_buffer,
        sizeof(ack_buffer),
        &ack_len
    );
    
    ASSERT_EQ(err, XGL_OK);
    ASSERT_EQ(ack_len, XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE);
    
    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, ack_buffer, ack_len), XGL_OK);

    EXPECT_EQ(header.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(header.source_id, 0x02U);  /* Swapped */
    EXPECT_EQ(header.target_id, 0x01U);  /* Swapped */
    EXPECT_EQ(header.payload_len, 0U);
    EXPECT_NE(header.flags & XGL_WIRE_FLAG_CONTROL, 0U);
    
    /* Verify CRC16 */
    uint16_t calc_crc16 = xgl_crc16_modbus(ack_buffer, XGL_FRAME_HEADER_SIZE);
    uint16_t frame_crc16 = xgl_deserialize_u16_le(ack_buffer + XGL_FRAME_HEADER_SIZE);
    EXPECT_EQ(frame_crc16, calc_crc16);
}

TEST_F(XglAckTest, GenerateAckNullPointer) {
    size_t ack_len = 0;
    
    /* Test null buffer */
    xgl_error_t err = xgl_ack_generate(0, 0x01, 0x02, nullptr, 64, &ack_len);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    /* Test null length */
    uint8_t buffer[64];
    err = xgl_ack_generate(0, 0x01, 0x02, buffer, sizeof(buffer), nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglAckTest, GenerateAckBufferTooSmall) {
    uint8_t ack_buffer[10];  /* Too small */
    size_t ack_len = 0;
    
    xgl_error_t err = xgl_ack_generate(
        0, 0x01, 0x02,
        ack_buffer,
        sizeof(ack_buffer),
        &ack_len
    );
    
    EXPECT_EQ(err, XGL_ERR_BUFFER_TOO_SMALL);
}

/*---------------------------------------------------------------------------*/
/* Duplicate Detection Tests                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglAckTest, DuplicateDetection) {
    /* Initially, no sequence numbers are marked as received */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 0));
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 42));
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 255));
    
    /* Mark sequence number 42 as received */
    xgl_error_t err = xgl_ack_mark_received(&handler, 42);
    ASSERT_EQ(err, XGL_OK);
    
    /* Now 42 should be detected as duplicate */
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 42));
    
    /* Other sequence numbers should still not be duplicates */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 0));
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 41));
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 43));
}

TEST_F(XglAckTest, DuplicateDetectionMultiple) {
    /* Mark multiple sequence numbers */
    xgl_ack_mark_received(&handler, 10);
    xgl_ack_mark_received(&handler, 20);
    xgl_ack_mark_received(&handler, 30);
    
    /* Verify all are detected as duplicates */
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 10));
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 20));
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 30));
    
    /* Verify others are not */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 11));
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 19));
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 31));
}

TEST_F(XglAckTest, DuplicateDetectionWraparound) {
    /* Test sequence number wraparound (0-255) */
    xgl_ack_mark_received(&handler, 0);
    xgl_ack_mark_received(&handler, 255);
    
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 0));
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 255));
}

TEST_F(XglAckTest, DuplicateDetectionIsScopedBySource) {
    ASSERT_EQ(xgl_ack_mark_received_from(&handler, 0x10, 42), XGL_OK);

    EXPECT_TRUE(xgl_ack_is_duplicate_from(&handler, 0x10, 42));
    EXPECT_FALSE(xgl_ack_is_duplicate_from(&handler, 0x11, 42));

    ASSERT_EQ(xgl_ack_mark_received_from(&handler, 0x11, 42), XGL_OK);
    EXPECT_TRUE(xgl_ack_is_duplicate_from(&handler, 0x10, 42));
    EXPECT_TRUE(xgl_ack_is_duplicate_from(&handler, 0x11, 42));
}

/*---------------------------------------------------------------------------*/
/* Out-of-Order Detection Tests                                             */
/*---------------------------------------------------------------------------*/

TEST_F(XglAckTest, OutOfOrderDetection) {
    /* Expected sequence number is 0 initially */
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 0));  /* In order */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 1));   /* Out of order (future) */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 5));   /* Out of order (future) */
    
    /* Update expected to 10 */
    xgl_ack_update_expected(&handler, 9);
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 10)); /* In order */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 11));  /* Out of order (future) */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 9));   /* Out of order (past) */
}

TEST_F(XglAckTest, OutOfOrderWindow) {
    /* Set expected to 50 */
    xgl_ack_update_expected(&handler, 49);
    
    /* Within window (128 packets) should be detected as out-of-order */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 51));  /* +1 */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 60));  /* +10 */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 49));  /* -1 */
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 40));  /* -10 */
    
    /* Exactly at window boundary (+/-128) should NOT be detected */
    /* 50 + 128 = 178, diff = 128, not < 128, so not out-of-order */
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 178)); /* Exactly +128 or -128 */
    /* 50 - 129 wraps to 177, diff = 177-50 = 127, which is < 128, so IS out-of-order */
    /* 50 + 129 = 179, diff = 129, wraps to -127, which is > -128, so IS out-of-order */
    /* We need something further: 50 + 130 = 180 */
    /* diff = 180 - 50 = 130, wraps to 130 - 256 = -126, which is > -128, still out-of-order */
    /* Actually, the boundary is exactly at +/-128, so 178 should work */
}

/*---------------------------------------------------------------------------*/
/* Expected Sequence Number Tests                                           */
/*---------------------------------------------------------------------------*/

TEST_F(XglAckTest, UpdateExpectedSequence) {
    /* Initial expected is 0 */
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 0));
    
    /* Update to 1 */
    xgl_ack_update_expected(&handler, 0);
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 1));
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 0));
    
    /* Update to 2 */
    xgl_ack_update_expected(&handler, 1);
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 2));
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 1));
}

TEST_F(XglAckTest, UpdateExpectedWraparound) {
    /* Update to 255 */
    xgl_ack_update_expected(&handler, 254);
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 255));
    
    /* Update to 0 (wraparound) */
    xgl_ack_update_expected(&handler, 255);
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 0));
}

TEST_F(XglAckTest, ExpectedSequenceIsScopedBySource) {
    ASSERT_EQ(xgl_ack_update_expected_from(&handler, 0x10, 9), XGL_OK);

    EXPECT_FALSE(xgl_ack_is_out_of_order_from(&handler, 0x10, 10));
    EXPECT_TRUE(xgl_ack_is_out_of_order_from(&handler, 0x10, 9));

    EXPECT_FALSE(xgl_ack_is_out_of_order_from(&handler, 0x11, 0));
    EXPECT_TRUE(xgl_ack_is_out_of_order_from(&handler, 0x11, 10));
}

/*---------------------------------------------------------------------------*/
/* ACK Processing Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglAckTest, ProcessAck) {
    bool is_valid = false;
    
    /* Process ACK for sequence number 42 */
    xgl_error_t err = xgl_ack_process(&handler, 42, 0x01, &is_valid);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_TRUE(is_valid);
}

TEST_F(XglAckTest, ProcessAckNullPointer) {
    /* Test null handler */
    bool is_valid = false;
    xgl_error_t err = xgl_ack_process(nullptr, 0, 0x01, &is_valid);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    /* Test null is_valid */
    err = xgl_ack_process(&handler, 0, 0x01, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Reset Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(XglAckTest, Reset) {
    /* Mark some sequence numbers as received */
    xgl_ack_mark_received(&handler, 10);
    xgl_ack_mark_received(&handler, 20);
    xgl_ack_mark_received(&handler, 30);
    
    /* Update expected sequence number */
    xgl_ack_update_expected(&handler, 50);
    
    /* Verify state */
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 10));
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 20));
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 51));
    
    /* Reset */
    xgl_ack_reset(&handler);
    
    /* Verify state is cleared */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 10));
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 20));
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 0));
}

/*---------------------------------------------------------------------------*/
/* Integration Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglAckTest, FullAckCycle) {
    /* Simulate receiving packets and generating ACKs */
    
    /* Receive packet with seq_num 0 */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 0));
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 0));
    xgl_ack_mark_received(&handler, 0);
    xgl_ack_update_expected(&handler, 0);
    
    /* Generate ACK */
    uint8_t ack_buffer[64];
    size_t ack_len = 0;
    xgl_error_t err = xgl_ack_generate(0, 0x01, 0x02, ack_buffer, sizeof(ack_buffer), &ack_len);
    ASSERT_EQ(err, XGL_OK);
    
    /* Receive packet with seq_num 1 */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 1));
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 1));
    xgl_ack_mark_received(&handler, 1);
    xgl_ack_update_expected(&handler, 1);
    
    /* Receive duplicate packet with seq_num 0 */
    EXPECT_TRUE(xgl_ack_is_duplicate(&handler, 0));
    /* Should still send ACK for duplicate */
    err = xgl_ack_generate(0, 0x01, 0x02, ack_buffer, sizeof(ack_buffer), &ack_len);
    ASSERT_EQ(err, XGL_OK);
    
    /* Receive out-of-order packet with seq_num 3 (expected 2) */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 3));
    EXPECT_TRUE(xgl_ack_is_out_of_order(&handler, 3));
    xgl_ack_mark_received(&handler, 3);
    /* Don't update expected yet, waiting for seq_num 2 */
    
    /* Receive missing packet with seq_num 2 */
    EXPECT_FALSE(xgl_ack_is_duplicate(&handler, 2));
    EXPECT_FALSE(xgl_ack_is_out_of_order(&handler, 2));
    xgl_ack_mark_received(&handler, 2);
    xgl_ack_update_expected(&handler, 2);
    
    /* Now can advance expected to 4 since we have 2 and 3 */
    xgl_ack_update_expected(&handler, 3);
}

/*---------------------------------------------------------------------------*/
/* Main                                                                      */
/*---------------------------------------------------------------------------*/

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
