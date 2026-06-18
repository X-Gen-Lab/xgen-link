# Kconfig and Runtime Configuration

XGL has three configuration mechanisms: Kconfig (compile-time), `xgl_config.h` (compile-time defaults), and `xgl_config_t` (runtime). Understanding the relationship between all three is essential for correctly trimming and configuring the protocol stack.

## Three Configuration Mechanisms

| Mechanism | File | When Applied | Description |
| --- | --- | --- | --- |
| Kconfig | `Kconfig` | Compile-time | Defines `XGL_*` macros, injected via CMake/menuconfig |
| Config Header | `include/xgl/xgl_config.h` | Compile-time | Defines `XGL_DEFAULT_*` fallback values when Kconfig is absent |
| Runtime Config | `xgl_config_t` | Runtime | Passed to `xgl_init()`, overrides compile-time defaults |

### Configuration Priority

1. Kconfig macros have highest priority (via `-DXGL_XXX=value` compiler flags).
2. When Kconfig is absent, `xgl_config.h` `#ifndef` defaults take effect.
3. Runtime `xgl_config_t` fields can override some compile-time defaults.

## Kconfig Groups

### Protocol Core

| Config | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `XGL_ENABLE` | bool | y | — | Enable protocol stack |
| `XGL_MAX_INSTANCES` | int | 4 | 1-16 | Maximum instances |
| `XGL_MAX_FRAME_SIZE` | int | 256 | 64-1024 | Maximum frame size (header + CRC) |
| `XGL_THREAD_SAFE` | bool | n | — | Enable mutex protection |
| `XGL_ENABLE_FRAGMENTATION` | bool | y | — | Enable fragmentation |
| `XGL_ENABLE_LOGGING` | bool | n | — | Enable logging |
| `XGL_ENABLE_STATISTICS` | bool | y | — | Enable statistics |
| `XGL_ENABLE_ASSERTIONS` | bool | y | — | Enable runtime assertions |

### Memory

| Config | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `XGL_DEFAULT_TX_POOL_SIZE` | int | 4096 | 1024-65536 | TX memory pool (bytes) |
| `XGL_DEFAULT_RX_BUFFER_SIZE` | int | 512 | 128-4096 | RX buffer (bytes) |
| `XGL_MEMORY_POOL_ALIGNMENT` | int | 4 | 1-16 | Pool alignment (bytes) |

### Transport

| Config | Type | Default | Range | Description |
| --- | --- | --- | --- | --- |
| `XGL_DEFAULT_ACK_TIMEOUT_MS` | int | 100 | 10-5000 | ACK timeout (ms) |
| `XGL_DEFAULT_MAX_RETRY` | int | 3 | 0-10 | Max retry count |
| `XGL_DEFAULT_WINDOW_SIZE` | int | 4 | 1-16 | Sliding window size |

### Compression & Encryption

| Config | Type | Default | Description |
| --- | --- | --- | --- |
| `XGL_ENABLE_COMPRESSION` | bool | n | Enable compression |
| `XGL_COMPRESSION_RLE` / `LZ77` / `ZLIB` | choice | RLE | Compression algorithm |
| `XGL_ENABLE_ENCRYPTION` | bool | n | Enable encryption |
| `XGL_ENCRYPTION_AES128` / `CHACHA20` | choice | AES128 | Encryption algorithm |

### Resource Constraints

| Config | Type | Default | Description |
| --- | --- | --- | --- |
| `XGL_TINY_FOOTPRINT` | bool | n | Minimal code/RAM |
| `XGL_SMALL_FOOTPRINT` | bool | n | Small code/RAM |
| `XGL_MEDIUM_FOOTPRINT` | bool | y | Balanced (default) |
| `XGL_LARGE_FOOTPRINT` | bool | n | Full-featured |

## Kconfig vs xgl_config.h Default Differences

!!! warning "Default value mismatches"
    Some parameters have different default values between Kconfig and `xgl_config.h`. Kconfig values apply when building with CMake/menuconfig; `xgl_config.h` values apply when compiling directly without Kconfig.

| Parameter | Kconfig Default | xgl_config.h Default | Note |
| --- | --- | --- | --- |
| `ACK_TIMEOUT_MS` | 100 | 1000 | Kconfig is more aggressive |
| `TX_POOL_SIZE` | 4096 | 2048 | Kconfig allocates more |
| `RX_BUFFER_SIZE` | 512 | 288 | Kconfig allocates more |
| `MAX_RETRY` | 3 | 5 | Kconfig is more conservative |

**Recommendation**: Use Kconfig values when building with CMake; be aware of `xgl_config.h` defaults for direct compilation.

## Runtime Configuration

### Key xgl_config_t Fields

- `source_id` (uint16_t): Local node ID, required at runtime.
- `max_retry_count` (uint8_t): Max retransmission attempts.
- `default_timeout_ms` (uint32_t): Default timeout in milliseconds.
- `window_size` (uint8_t): Sliding window size.
- `enable_fragmentation` (bool): Fragmentation toggle.
- `max_frame_size` (uint16_t): Maximum frame size.
- `routes[]` (xgl_route_item_t): Routing table, required at runtime.
- `memory.allocator` (xgl_allocator_t*): Memory allocator.
- `auth.auth_required` (bool): Authentication toggle.
- `auth.auth_provider` (xgl_auth_provider_t*): Authentication callback.
- `callbacks`: `rx_callback`, `error_callback`.

### Override Rules

Non-zero runtime values override compile-time defaults for: `max_retry_count`, `default_timeout_ms`, `window_size`. Fields like `source_id` and `routes[]` are runtime-only with no compile-time defaults.

## Trimming Guide

### Minimal (MCU, 64KB Flash)

`XGL_MAX_INSTANCES=1`, `MAX_FRAME_SIZE=128`, `THREAD_SAFE=n`, `FRAGMENTATION=n`, `TX_POOL=1024`, `RX_BUFFER=128`, `TINY_FOOTPRINT=y`

### Typical (MCU, 256KB Flash)

`XGL_MAX_INSTANCES=2`, `MAX_FRAME_SIZE=256`, `THREAD_SAFE=y`, `FRAGMENTATION=y`, `TX_POOL=4096`, `RX_BUFFER=512`, `MEDIUM_FOOTPRINT=y`

### Full-Featured (Desktop/Gateway)

`XGL_MAX_INSTANCES=8`, `MAX_FRAME_SIZE=1024`, `THREAD_SAFE=y`, `FRAGMENTATION=y`, `LOGGING=y`, `QOS=y`, `TX_POOL=16384`, `RX_BUFFER=2048`, `LARGE_FOOTPRINT=y`

## Evidence

| Rule | Source |
| --- | --- |
| Kconfig definitions | `Kconfig` (285 lines) |
| Config header defaults | `include/xgl/xgl_config.h` |
| Runtime config validation | `src/api/xgl_config.c` |
| CMake integration | `CMakeLists.txt` |