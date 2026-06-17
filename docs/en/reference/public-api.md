# Public API

Normal SDK users depend only on installed public headers:

- `xgl.h`
- `xgl_config.h`
- `xgl_types.h`
- `xgl_error.h`

Internal protocol headers live under `include/xgl/internal`, for example `xgl/internal/xgl_wire.h`, `xgl/internal/xgl_parser.h`, and `xgl/internal/xgl_reliable.h`. They are not installed as normal SDK headers and are not stable user APIs.

## API Groups

- Lifecycle: `xgl_create`, `xgl_init`, `xgl_destroy`
- Send: `xgl_send`, `xgl_send_zerocopy`
- Runtime: `xgl_run`, `xgl_next_deadline_ms`
- Stats: `xgl_stats_get`, `xgl_stats_reset`
- Version: `xgl_version_string`, `xgl_version_int`

## Minimal Lifecycle

```c
#include "xgl/xgl.h"

static xgl_error_t phy_tx(const uint8_t* data, size_t len, void* user_data);
static xgl_error_t phy_rx(uint8_t* buffer, size_t* len, void* user_data);

static void on_rx(xgl_handle_t handle, uint16_t source_id, uint8_t data_type,
                  const uint8_t* data, size_t len, void* user_data);

xgl_phy_ops_t phy = {
    .tx = phy_tx,
    .rx = phy_rx,
    .user_data = NULL,
};

xgl_route_item_t routes[] = {
    { .target_id = 2, .phy = &phy, .max_frame_size = 256, .read_freq_hz = 100, .metric = 1 },
};

xgl_config_t config;
xgl_config_get_default(&config);
config.source_id = 1;
config.route_table = routes;
config.route_table_len = 1;
config.rx_callback = on_rx;

xgl_handle_t handle = xgl_create(&config);
if (handle != NULL && xgl_init(handle) == XGL_OK) {
    xgl_run(handle, 100);
}
xgl_destroy(handle);
```

## Send

```c
const uint8_t payload[] = "hello";
xgl_tx_data_t tx = {
    .target_id = 2,
    .data_type = 1,
    .data = payload,
    .data_len = sizeof(payload) - 1,
    .reliable = true,
    .priority = 0,
    .timeout_ms = 0,
    .connection_id = 0,
    .session_epoch = 0,
};

xgl_error_t err = xgl_send(handle, &tx);
```

Use `xgl_send_zerocopy()` only for single-frame unreliable sends where the
caller buffer reserves the documented header/TLV space.

## Stats

```c
xgl_statistics_t stats;
if (xgl_stats_get(handle, &stats) == XGL_OK) {
    printf("transport tx=%llu rx=%llu retries=%llu\n",
           (unsigned long long)stats.transport.tx_packets,
           (unsigned long long)stats.transport.rx_packets,
           (unsigned long long)stats.tx_retries);
}
```

## Authentication Provider

```c
static xgl_error_t sign(uint32_t key_id, const uint8_t* aad, size_t aad_len,
                        const uint8_t* payload, size_t payload_len,
                        uint8_t* tag, size_t tag_capacity, size_t* tag_len,
                        void* user_data);

static xgl_error_t verify(uint32_t key_id, const uint8_t* aad, size_t aad_len,
                          const uint8_t* payload, size_t payload_len,
                          const uint8_t* tag, size_t tag_len, bool* valid,
                          void* user_data);

xgl_auth_provider_t provider = {
    .sign = sign,
    .verify = verify,
    .tag_len = 16,
    .user_data = NULL,
};

config.auth_required = true;
config.auth_key_id = 1;
config.auth_provider = &provider;
config.memory.allocator = &allocator;
```

When `auth_required=true`, `config.memory.allocator` must provide both
`malloc` and `free`.

## Doxygen

The CMake documentation build generates the public C API reference:

<a href="../../../api/doxygen/html/index.html">Open generated Doxygen API</a>
