#include <gtest/gtest.h>
#include <xgl/xgl_security.h>

TEST(XglSecurityTest, ReplayWindowAcceptsNewPacketsAndRejectsDuplicates) {
    xgl_replay_window_t window = {};
    ASSERT_EQ(xgl_replay_window_init(&window, 0x1234, 0x01020304U, 7, 64), XGL_OK);

    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 11));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 8, 12));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x9999, 0x01020304U, 7, 12));
}

TEST(XglSecurityTest, ReplayWindowRejectsPacketsOlderThanWindow) {
    xgl_replay_window_t window = {};
    ASSERT_EQ(xgl_replay_window_init(&window, 0x1234, 0x01020304U, 7, 64), XGL_OK);

    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 80));

    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 10));
    EXPECT_TRUE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 79));
    EXPECT_FALSE(xgl_replay_window_accept(&window, 0x1234, 0x01020304U, 7, 79));
}

TEST(XglSecurityTest, ReplayWindowValidatesParameters) {
    xgl_replay_window_t window = {};

    EXPECT_EQ(xgl_replay_window_init(nullptr, 1, 1, 1, 64), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_replay_window_init(&window, 0, 1, 1, 64), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_replay_window_init(&window, 1, 1, 1, 0), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_replay_window_init(&window, 1, 1, 1, 65), XGL_ERR_INVALID_PARAM);
    EXPECT_FALSE(xgl_replay_window_accept(nullptr, 1, 1, 1, 1));
}
