/**
 * \file            test_codec.cpp
 * \brief           Codec module tests
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_codec.h>
#include <cstring>

namespace {

static xgl_error_t copy_codec(const uint8_t* input,
                              size_t input_len,
                              uint8_t* output,
                              size_t* output_len,
                              void* user_data) {
    (void)user_data;
    if (input == nullptr || output == nullptr || output_len == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    if (*output_len < input_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    std::memcpy(output, input, input_len);
    *output_len = input_len;
    return XGL_OK;
}

}  // namespace

TEST(XglCodecTest, RegisterAndApplyCompressionCodec) {
    xgl_codec_t storage[2];
    xgl_codec_registry_t registry;
    ASSERT_EQ(xgl_codec_registry_init(&registry, storage, 2), XGL_OK);

    xgl_codec_t codec = {
        .id = 1,
        .kind = XGL_CODEC_KIND_COMPRESSION,
        .encode = copy_codec,
        .decode = copy_codec,
        .user_data = nullptr
    };
    EXPECT_EQ(xgl_codec_register(&registry, &codec), XGL_OK);

    const uint8_t input[] = {'x', 'g', 'l'};
    uint8_t output[sizeof(input)] = {};
    size_t output_len = sizeof(output);

    EXPECT_EQ(xgl_codec_apply(&registry,
                              XGL_CODEC_KIND_COMPRESSION,
                              1,
                              XGL_CODEC_DIRECTION_ENCODE,
                              input,
                              sizeof(input),
                              output,
                              &output_len),
              XGL_OK);
    EXPECT_EQ(output_len, sizeof(input));
    EXPECT_EQ(std::memcmp(input, output, sizeof(input)), 0);
}

TEST(XglCodecTest, MissingCodecDoesNotFallThroughBaseLink) {
    xgl_codec_t storage[1];
    xgl_codec_registry_t registry;
    ASSERT_EQ(xgl_codec_registry_init(&registry, storage, 1), XGL_OK);

    const uint8_t input[] = {'x'};
    uint8_t output[1] = {};
    size_t output_len = sizeof(output);

    EXPECT_EQ(xgl_codec_apply(&registry,
                              XGL_CODEC_KIND_ENCRYPTION,
                              7,
                              XGL_CODEC_DIRECTION_ENCODE,
                              input,
                              sizeof(input),
                              output,
                              &output_len),
              XGL_ERR_INVALID_PARAM);
}
