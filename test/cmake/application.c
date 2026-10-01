/**
 * \file            application.c
 * \brief           Consume additional capabilities selected by the parent
 */

#include <xgen/containers/ring_buffer.h>
#include <xgen/memory/arena.h>
#include <xgl/xgl.h>

int main(void) {
    uint8_t storage[8] = {0};
    uint8_t output[2] = {0};
    xgct_ring_buffer_t ring;
    xgm_arena_t arena;

    if (xgct_ring_init(&ring, storage, sizeof(storage)) != XGS_OK ||
        xgct_ring_write(&ring, "ok", 2U) != 2U ||
        xgct_ring_read(&ring, output, sizeof(output)) != sizeof(output)) {
        return 1;
    }
    if (xgct_ring_deinit(&ring) != XGS_OK ||
        xgm_arena_init(&arena, storage, sizeof(storage)) != XGS_OK ||
        xgm_arena_alloc(&arena, 1U, 1U) == NULL) {
        return 2;
    }

    xgm_arena_deinit(&arena);
    return output[0] != 'o' || output[1] != 'k' ||
           xgl_version_int() != XGL_VERSION_INT;
}
