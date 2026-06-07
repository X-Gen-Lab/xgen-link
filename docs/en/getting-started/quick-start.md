# Quick Start

## Install Documentation Dependencies

```sh
python -m pip install -r docs/requirements.txt
```

Documentation builds also require Doxygen. Release validation environments require `cppcheck`.

## Build and Test

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_tests
ctest --preset gcc-test --output-on-failure
```

## Minimal Program

```c
#include <xgl/xgl.h>

static xgl_error_t phy_tx(const uint8_t* data, size_t len, void* user_data) {
    (void)data;
    (void)len;
    (void)user_data;
    return XGL_OK;
}

static xgl_error_t phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    (void)buffer;
    (void)user_data;
    *len = 0;
    return XGL_OK;
}

int main(void) {
    xgl_phy_ops_t phy = { .tx = phy_tx, .rx = phy_rx, .user_data = 0 };
    xgl_route_item_t route = { .target_id = 2, .phy = &phy, .max_frame_size = 256, .read_freq_hz = 100 };

    xgl_config_t config;
    xgl_config_get_default(&config);
    config.source_id = 1;
    config.route_table = &route;
    config.route_table_len = 1;

    xgl_handle_t handle = xgl_create(&config);
    if (handle == 0) {
        return 1;
    }
    if (xgl_init(handle) != XGL_OK) {
        xgl_destroy(handle);
        return 1;
    }

    xgl_run(handle, 100);
    xgl_destroy(handle);
    return 0;
}
```

When production authentication is enabled, configure an `auth_provider` before initialization.
