/**
 * \file            consumer.c
 * \brief           Exercise the public SDK from C and C++
 */

#include <xgl/xgl.h>

#include <stdalign.h>

static xgl_error_t consumer_transmit(const uint8_t* data, size_t size,
                                     void* ctx) {
    (void)data;
    (void)size;
    (void)ctx;
    return XGL_OK;
}

static xgl_error_t consumer_receive(uint8_t* data, size_t* size, void* ctx) {
    (void)data;
    (void)ctx;
    *size = 0U;
    return XGL_OK;
}

int main(void) {
    alignas(xgl_max_align_t) static uint8_t workspace[16384];
    xgl_phy_ops_t phy = {consumer_transmit, consumer_receive, NULL};
    xgl_route_item_t route = {2U, &phy, 64U, 100U, 0U};
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    config.source_id = 1U;
    config.protocol.max_frame_size = route.max_frame_size;
    config.route_table = &route;
    config.route_table_len = 1U;

    xgl_handle_t handle = NULL;
    if (xgl_version_int() != XGL_VERSION_INT ||
        xgl_create_with_error_checked(&config, sizeof(config), 4U,
                                      XGL_BUILD_CONFIG_ID,
                                      &handle) != XGL_ERR_INVALID_VERSION ||
        handle != NULL) {
        return 1;
    }
    if (xgl_memory_requirements_checked(
            &config, sizeof(config) - 1U, XGL_CONFIG_ABI_VERSION,
            XGL_BUILD_CONFIG_ID, NULL) != XGL_ERR_INVALID_VERSION) {
        return 2;
    }

    xgl_memory_requirements_t requirements;
    if (xgl_memory_requirements_checked(
            &config, sizeof(config), XGL_CONFIG_ABI_VERSION,
            XGL_BUILD_CONFIG_ID ^ 1U,
            &requirements) != XGL_ERR_INVALID_VERSION) {
        return 3;
    }
    if (xgl_memory_requirements(&config, &requirements) != XGL_OK ||
        requirements.size == 0U || requirements.size > sizeof(workspace) ||
        xgl_init_static(&config, workspace, requirements.size, &handle) !=
            XGL_OK ||
        handle == NULL) {
        return 4;
    }
    xgl_destroy(handle);
    return 0;
}
