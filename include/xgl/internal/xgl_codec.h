/**
 * \file            xgl_codec.h
 * \brief           Optional codec module boundary
 */

#ifndef XGL_CODEC_H
#define XGL_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl/xgl_error.h"

typedef enum {
    XGL_CODEC_KIND_COMPRESSION = 1,
    XGL_CODEC_KIND_ENCRYPTION = 2
} xgl_codec_kind_t;

typedef enum {
    XGL_CODEC_DIRECTION_ENCODE = 1,
    XGL_CODEC_DIRECTION_DECODE = 2
} xgl_codec_direction_t;

typedef xgl_error_t (*xgl_codec_process_fn)(const uint8_t* input,
                                            size_t input_len,
                                            uint8_t* output,
                                            size_t* output_len,
                                            void* user_data);

typedef struct {
    uint8_t id;
    xgl_codec_kind_t kind;
    xgl_codec_process_fn encode;
    xgl_codec_process_fn decode;
    void* user_data;
} xgl_codec_t;

typedef struct {
    xgl_codec_t* codecs;
    size_t codec_count;
    size_t codec_capacity;
} xgl_codec_registry_t;

xgl_error_t xgl_codec_registry_init(xgl_codec_registry_t* registry,
                                    xgl_codec_t* storage,
                                    size_t capacity);

xgl_error_t xgl_codec_register(xgl_codec_registry_t* registry,
                               const xgl_codec_t* codec);

const xgl_codec_t* xgl_codec_find(const xgl_codec_registry_t* registry,
                                  xgl_codec_kind_t kind,
                                  uint8_t id);

xgl_error_t xgl_codec_apply(const xgl_codec_registry_t* registry,
                            xgl_codec_kind_t kind,
                            uint8_t id,
                            xgl_codec_direction_t direction,
                            const uint8_t* input,
                            size_t input_len,
                            uint8_t* output,
                            size_t* output_len);

#ifdef __cplusplus
}
#endif

#endif /* XGL_CODEC_H */
