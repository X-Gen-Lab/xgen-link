/**
 * \file            consumer.c
 * \brief           Exercise the public SDK from C and C++
 */

#include <xgl/xgl.h>

int main(void) {
    const xgl_config_t config = {0};
    xgl_handle_t handle = NULL;
    if (xgl_version_int() != XGL_VERSION_INT ||
        xgl_create_with_error_checked(&config, sizeof(config), 3U,
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
    return 0;
}
