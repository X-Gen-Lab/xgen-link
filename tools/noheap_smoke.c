#include <xgl/xgl_allocator.h>
#include <xgl/xgl_reliable.h>
#include <xgl/xgl_tiered_pool.h>

int main(void) {
#if XGL_ALLOW_FALLBACK_MALLOC != 0
#error "noheap smoke must compile xgl with fallback malloc disabled"
#endif

    void* ptr = xgl_alloc(NULL, 16U);
    if (ptr != NULL) {
        return 1;
    }

    if (xgl_allocator_get_default() != NULL) {
        return 6;
    }

    xgl_tracking_allocator_t tracker;
    if (xgl_tracking_allocator_init(&tracker, NULL) == 0) {
        return 2;
    }

    xgl_reliable_queue_t queue;
    uint8_t data[] = { 0x01U, 0x02U, 0x03U };

    if (xgl_reliable_init(&queue, 3U, NULL) != XGL_OK) {
        return 3;
    }

    if (xgl_reliable_add_packet_number(&queue,
                                       data,
                                       sizeof(data),
                                       0x0001U,
                                       0x0002U,
                                       1U,
                                       0U,
                                       0U,
                                       1000,
                                       NULL) != XGL_ERR_NO_MEMORY) {
        return 4;
    }

    xgl_reliable_destroy(&queue);

    xgl_tiered_pool_t tiered_pool;
    if (xgl_tiered_pool_init(&tiered_pool, 1U, 0U, 0U) == 0) {
        xgl_tiered_pool_destroy(&tiered_pool);
        return 5;
    }

    uint8_t small_pool[XGL_TIERED_POOL_SMALL_SIZE];
    if (xgl_tiered_pool_init_static(&tiered_pool,
                                    small_pool,
                                    1U,
                                    NULL,
                                    0U,
                                    NULL,
                                    0U) != 0) {
        return 7;
    }

    void* tiered_ptr = xgl_tiered_pool_alloc(&tiered_pool, 16U);
    if (tiered_ptr == NULL) {
        xgl_tiered_pool_destroy(&tiered_pool);
        return 8;
    }
    xgl_tiered_pool_free(&tiered_pool, tiered_ptr, 16U);
    xgl_tiered_pool_destroy(&tiered_pool);

    return 0;
}
