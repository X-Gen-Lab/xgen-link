/**
 * \file            xgl_codec.c
 * \brief           Optional codec module registry
 */

#include "xgl/xgl_codec.h"
#include <string.h>

xgl_error_t xgl_codec_registry_init(xgl_codec_registry_t* registry,
                                    xgl_codec_t* storage,
                                    size_t capacity) {
    if (registry == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (storage == NULL && capacity > 0U) {
        return XGL_ERR_NULL_POINTER;
    }

    registry->codecs = storage;
    registry->codec_count = 0;
    registry->codec_capacity = capacity;
    if (storage != NULL) {
        memset(storage, 0, sizeof(xgl_codec_t) * capacity);
    }

    return XGL_OK;
}

xgl_error_t xgl_codec_register(xgl_codec_registry_t* registry,
                               const xgl_codec_t* codec) {
    if (registry == NULL || codec == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (codec->encode == NULL || codec->decode == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (registry->codec_count >= registry->codec_capacity) {
        return XGL_ERR_QUEUE_FULL;
    }
    if (xgl_codec_find(registry, codec->kind, codec->id) != NULL) {
        return XGL_ERR_ALREADY_INITIALIZED;
    }

    registry->codecs[registry->codec_count] = *codec;
    registry->codec_count++;
    return XGL_OK;
}

const xgl_codec_t* xgl_codec_find(const xgl_codec_registry_t* registry,
                                  xgl_codec_kind_t kind,
                                  uint8_t id) {
    if (registry == NULL || registry->codecs == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < registry->codec_count; i++) {
        const xgl_codec_t* codec = &registry->codecs[i];
        if (codec->kind == kind && codec->id == id) {
            return codec;
        }
    }

    return NULL;
}

xgl_error_t xgl_codec_apply(const xgl_codec_registry_t* registry,
                            xgl_codec_kind_t kind,
                            uint8_t id,
                            xgl_codec_direction_t direction,
                            const uint8_t* input,
                            size_t input_len,
                            uint8_t* output,
                            size_t* output_len) {
    if (input == NULL || output == NULL || output_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    const xgl_codec_t* codec = xgl_codec_find(registry, kind, id);
    if (codec == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (direction == XGL_CODEC_DIRECTION_ENCODE) {
        return codec->encode(input, input_len, output, output_len, codec->user_data);
    }
    if (direction == XGL_CODEC_DIRECTION_DECODE) {
        return codec->decode(input, input_len, output, output_len, codec->user_data);
    }

    return XGL_ERR_INVALID_PARAM;
}
