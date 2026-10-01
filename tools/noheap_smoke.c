#include <xgl/xgl.h>

#include <stddef.h>
#include <string.h>
#include <xgen/memory/allocator.h>

typedef struct {
    xgm_max_align_t alignment;
    unsigned char bytes[16384];
    size_t used;
} smoke_storage_t;

static void* smoke_alloc(void* ctx, size_t size) {
    smoke_storage_t* storage = (smoke_storage_t*)ctx;
    const size_t alignment = _Alignof(xgm_max_align_t);
    size_t start = (storage->used + alignment - 1U) & ~(alignment - 1U);
    if (start > sizeof(storage->bytes) ||
        size > sizeof(storage->bytes) - start) {
        return NULL;
    }
    storage->used = start + size;
    return &storage->bytes[start];
}

static void smoke_free(void* ctx, void* ptr) {
    (void)ctx;
    (void)ptr;
}

static unsigned transmitted;

static xgl_error_t smoke_tx(const uint8_t* data, size_t len, void* ctx) {
    (void)ctx;
    if (data == NULL || len < XGL_FRAME_HEADER_SIZE)
        return XGL_ERR_INVALID_FRAME;
    ++transmitted;
    return XGL_OK;
}

static xgl_error_t smoke_rx(uint8_t* data, size_t* len, void* ctx) {
    (void)data;
    (void)ctx;
    *len = 0;
    return XGL_OK;
}

static int smoke_instance(void) {
    static smoke_storage_t storage;
    xgm_allocator_t core = {&storage, smoke_alloc, smoke_free};
    xgl_phy_ops_t phy = {smoke_tx, smoke_rx, NULL};
    xgl_route_item_t route = {2U, &phy, 128U, 100U, 1U};
    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.source_id = 1U;
    config.memory.allocator = &core;
    config.protocol.window_size = 1U;
    config.features.max_peers = 1U;
    config.route_table = &route;
    config.route_table_len = 1U;
    xgl_handle_t handle = xgl_create(&config);
    if (handle == NULL)
        return 10;
    if (xgl_init(handle) != XGL_OK) {
        xgl_destroy(handle);
        return 11;
    }
    const uint8_t payload[] = {1U, 2U, 3U};
    xgl_tx_data_t tx = {0};
    tx.target_id = 2U;
    tx.data = payload;
    tx.data_len = sizeof(payload);
    tx.reliable = true;
    int result =
        xgl_send_at(handle, &tx, 0U) == XGL_OK && transmitted != 0U ? 0 : 12;
    const xgl_work_budget_t budget = {128U, 1000U};
    (void)xgl_step(handle, 0U, &budget);
    xgl_destroy(handle);
    return result;
}

int main(void) {
#if XGL_ALLOW_FALLBACK_MALLOC != 0
#error "noheap smoke must compile xgl with fallback malloc disabled"
#endif

    void* ptr = xgm_alloc(NULL, 16U);
    if (ptr != NULL) {
        return 1;
    }

    return smoke_instance();
}
