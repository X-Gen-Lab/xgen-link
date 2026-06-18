# Kconfig 与运行时配置

XGL 有三套配置机制: Kconfig(编译时)、`xgl_config.h`(编译时默认值)和 `xgl_config_t`(运行时)。理解三者的关系是正确裁剪和配置协议栈的前提。

## 三套配置机制

| 机制 | 文件 | 作用时机 | 说明 |
| --- | --- | --- | --- |
| Kconfig | `Kconfig` | 编译时 | 定义 `XGL_*` 宏,通过 CMake/menuconfig 注入 |
| Config Header | `include/xgl/xgl_config.h` | 编译时 | 定义 `XGL_DEFAULT_*` 默认值,Kconfig 未定义时生效 |
| Runtime Config | `xgl_config_t` | 运行时 | `xgl_init()` 时传入,覆盖编译时默认值 |

### 配置优先级

```text
Kconfig 宏 (#define XGL_XXX)
  ↓ 未定义?
xgl_config.h 默认值 (#ifndef XGL_XXX)
  ↓ 运行时?
xgl_config_t 运行时字段 (config->xxx)
```

1. Kconfig 宏优先级最高,通过 `-DXGL_XXX=value` 编译参数注入。
2. Kconfig 未定义时,`xgl_config.h` 的 `#ifndef` 默认值生效。
3. 运行时 `xgl_config_t` 字段可以覆盖部分编译时默认值。

## Kconfig 分组

### 协议核心配置

| 配置项 | 类型 | 默认值 | 范围 | 说明 |
| --- | --- | --- | --- | --- |
| `XGL_ENABLE` | bool | y | — | 启用协议栈 |
| `XGL_MAX_INSTANCES` | int | 4 | 1-16 | 最大实例数 |
| `XGL_MAX_FRAME_SIZE` | int | 256 | 64-1024 | 最大帧大小(含 header 和 CRC) |
| `XGL_THREAD_SAFE` | bool | n | — | 启用 mutex 保护 |
| `XGL_ENABLE_FRAGMENTATION` | bool | y | — | 启用分片支持 |
| `XGL_ENABLE_LOGGING` | bool | n | — | 启用日志 |
| `XGL_ENABLE_STATISTICS` | bool | y | — | 启用统计收集 |
| `XGL_ENABLE_QOS` | bool | n | — | 启用 QoS 优先级 |
| `XGL_ENABLE_ASSERTIONS` | bool | y | — | 启用运行时断言 |

### 内存配置

| 配置项 | 类型 | 默认值 | 范围 | 说明 |
| --- | --- | --- | --- | --- |
| `XGL_DEFAULT_TX_POOL_SIZE` | int | 4096 | 1024-65536 | TX 内存池大小(字节) |
| `XGL_DEFAULT_RX_BUFFER_SIZE` | int | 512 | 128-4096 | RX 缓冲区大小(字节) |
| `XGL_MEMORY_POOL_ALIGNMENT` | int | 4 | 1-16 | 内存池对齐(字节) |

### 传输配置

| 配置项 | 类型 | 默认值 | 范围 | 说明 |
| --- | --- | --- | --- | --- |
| `XGL_DEFAULT_ACK_TIMEOUT_MS` | int | 100 | 10-5000 | 默认 ACK 超时(毫秒) |
| `XGL_DEFAULT_MAX_RETRY` | int | 3 | 0-10 | 默认最大重试次数 |
| `XGL_DEFAULT_WINDOW_SIZE` | int | 4 | 1-16 | 默认滑动窗口大小 |

### 压缩配置

| 配置项 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `XGL_ENABLE_COMPRESSION` | bool | n | 启用压缩 |
| `XGL_COMPRESSION_RLE` | bool | y | RLE 压缩(默认) |
| `XGL_COMPRESSION_LZ77` | bool | n | LZ77 压缩 |
| `XGL_COMPRESSION_ZLIB` | bool | n | ZLIB 压缩 |

### 加密配置

| 配置项 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `XGL_ENABLE_ENCRYPTION` | bool | n | 启用加密 |
| `XGL_ENCRYPTION_AES128` | bool | y | AES-128(默认) |
| `XGL_ENCRYPTION_CHACHA20` | bool | n | ChaCha20 |

### 平台配置

| 配置项 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `XGL_PLATFORM_POSIX` | bool | n | POSIX 平台 |
| `XGL_PLATFORM_FREERTOS` | bool | n | FreeRTOS 平台 |
| `XGL_PLATFORM_WINDOWS` | bool | n | Windows 平台 |
| `XGL_PLATFORM_BAREMETAL` | bool | n | 裸机平台 |

### 资源约束预设

| 配置项 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `XGL_TINY_FOOTPRINT` | bool | n | 最小代码/RAM |
| `XGL_SMALL_FOOTPRINT` | bool | n | 小代码/RAM |
| `XGL_MEDIUM_FOOTPRINT` | bool | y | 平衡配置(默认) |
| `XGL_LARGE_FOOTPRINT` | bool | n | 全功能配置 |

## Kconfig 与 xgl_config.h 默认值差异

!!! warning "默认值不一致"
    Kconfig 和 `xgl_config.h` 的部分默认值不同。Kconfig 值用于 CMake/menuconfig 构建;`xgl_config.h` 值用于不使用 Kconfig 的直接编译。

| 参数 | Kconfig 默认值 | xgl_config.h 默认值 | 说明 |
| --- | --- | --- | --- |
| `ACK_TIMEOUT_MS` | 100 | 1000 | Kconfig 更激进 |
| `TX_POOL_SIZE` | 4096 | 2048 | Kconfig 更大 |
| `RX_BUFFER_SIZE` | 512 | 288 | Kconfig 更大 |
| `MAX_RETRY` | 3 | 5 | Kconfig 更保守 |

**建议**:使用 CMake 构建时以 Kconfig 值为准;直接编译时注意 `xgl_config.h` 的默认值。

## 运行时配置

### xgl_config_t 关键字段

```text
xgl_config_t
├── source_id         (uint16_t — 本地节点 ID)
├── max_retry_count   (uint8_t — 最大重试)
├── default_timeout_ms(uint32_t — 默认超时)
├── window_size       (uint8_t — 滑动窗口)
├── enable_fragmentation(bool — 分片开关)
├── max_frame_size    (uint16_t — 最大帧)
├── routes[]          (xgl_route_item_t — 路由表)
├── memory.allocator  (xgl_allocator_t* — 内存分配器)
├── auth.auth_required(bool — 认证开关)
├── auth.auth_provider(xgl_auth_provider_t* — 认证回调)
└── callbacks         (rx_callback, error_callback)
```

### 运行时覆盖规则

- `max_retry_count`: 如果运行时值非 0,覆盖编译时默认值。
- `default_timeout_ms`: 如果运行时值非 0,覆盖编译时默认值。
- `window_size`: 如果运行时值非 0,覆盖编译时默认值。
- `source_id`: 运行时必填,编译时无默认值。
- `routes[]`: 运行时必填,编译时无默认值。

## 裁剪指南

### 最小配置(MCU, 64KB Flash)

```text
Kconfig:
  XGL_MAX_INSTANCES=1
  XGL_MAX_FRAME_SIZE=128
  XGL_THREAD_SAFE=n
  XGL_ENABLE_FRAGMENTATION=n
  XGL_ENABLE_LOGGING=n
  XGL_ENABLE_COMPRESSION=n
  XGL_ENABLE_ENCRYPTION=n
  XGL_DEFAULT_TX_POOL_SIZE=1024
  XGL_DEFAULT_RX_BUFFER_SIZE=128
  XGL_TINY_FOOTPRINT=y
```

### 典型配置(MCU, 256KB Flash)

```text
Kconfig:
  XGL_MAX_INSTANCES=2
  XGL_MAX_FRAME_SIZE=256
  XGL_THREAD_SAFE=y
  XGL_ENABLE_FRAGMENTATION=y
  XGL_DEFAULT_TX_POOL_SIZE=4096
  XGL_DEFAULT_RX_BUFFER_SIZE=512
  XGL_MEDIUM_FOOTPRINT=y
```

### 全功能配置(桌面/网关)

```text
Kconfig:
  XGL_MAX_INSTANCES=8
  XGL_MAX_FRAME_SIZE=1024
  XGL_THREAD_SAFE=y
  XGL_ENABLE_FRAGMENTATION=y
  XGL_ENABLE_LOGGING=y
  XGL_ENABLE_QOS=y
  XGL_DEFAULT_TX_POOL_SIZE=16384
  XGL_DEFAULT_RX_BUFFER_SIZE=2048
  XGL_LARGE_FOOTPRINT=y
```

## 证据

| 规则 | 源码 |
| --- | --- |
| Kconfig 定义 | `Kconfig` (285 行) |
| Config header 默认值 | `include/xgl/xgl_config.h` |
| 运行时配置校验 | `src/api/xgl_config.c` |
| CMake 集成 | `CMakeLists.txt` |