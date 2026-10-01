# 零拷贝

零拷贝发送 API 在调用者提供的可写存储中构造单帧非可靠消息，不引入异步缓冲所有权。

## 支持范围

| API | 作用域与用途 |
| --- | --- |
| `xgl_send_zerocopy_at()` | 保留原有描述符；connection ID 与 session epoch 均为零 |
| `xgl_send_zerocopy_scoped_at()` | 通过 `xgl_tx_zerocopy_request_t` 指定 connection ID 与 session epoch |
| `xgl_send_zerocopy_layout()` | 查询指定作用域请求的精确载荷偏移与完整帧大小 |

两个发送 API 均只支持单帧非可靠发送。`reliable=true` 返回 `XGL_ERR_INVALID_PARAM`，`timeout_ms` 不参与处理。可靠或分片消息使用 `xgl_send_at()`。认证发送要求已为选定的目标、connection ID 与 session epoch 安装可信会话。

## 缓冲布局

载荷前预留精确头部空间，载荷后预留尾部空间：

| 部分 | 字节数 |
| --- | ---: |
| 基础头 | `XGL_FRAME_HEADER_SIZE`（24） |
| 非零 data type | `XGL_DATA_TYPE_EXT_SIZE`（3） |
| 非零 session epoch | `XGL_SESSION_EXT_SIZE`（14） |
| 启用认证时的 security extension | 15 |
| 启用认证时的 tag | Provider tag 长度 |
| 末尾 CRC | `XGL_CRC16_SIZE`（2） |

Connection ID 位于基础头中，不增加扩展字节。应用通过 `xgl_send_zerocopy_layout()` 获取布局，避免自行重复计算。`data_offset` 必须等于 `layout.payload_offset`，`buffer_size` 必须覆盖 `layout.frame_size`。完整帧同时满足路由 MTU 和实例帧长限制。协议写入头部与尾部，载荷地址保持不变。

## 规划并发送

下面的 C11 示例要求实例已初始化并配置到节点 2 的路由。如果要求认证，应先安装对应的可信会话，再查询布局。

```c
#include <string.h>
#include <xgl/xgl.h>

xgl_error_t send_scoped_example(xgl_handle_t handle, uint32_t now_ms) {
    const uint8_t payload[] = {1U, 2U, 3U, 4U};
    uint8_t storage[128];
    xgl_tx_zerocopy_request_t request = {0};
    request.data.target_id = 2U;
    request.data.data_type = 1U;
    request.data.data_len = sizeof(payload);
    request.connection_id = 11U;
    request.session_epoch = 9U;

    xgl_tx_frame_layout_t layout;
    xgl_error_t error = xgl_send_zerocopy_layout(handle, &request, &layout);
    if (error != XGL_OK) {
        return error;
    }
    if (layout.frame_size > sizeof(storage)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(storage + layout.payload_offset, payload, sizeof(payload));
    request.data.buffer = storage;
    request.data.buffer_size = sizeof(storage);
    request.data.data_offset = layout.payload_offset;
    return xgl_send_zerocopy_scoped_at(handle, &request, now_ms);
}
```

查询忽略 `buffer`、`buffer_size` 和 `data_offset`，但 `data_len` 必须非零。失败时输出布局清零。查询不分配内存、不调用安全 provider 或 PHY、不更新发送统计，也不消耗安全序号。查询成功不预留资源，实际发送仍会重新检查当前路由、容量与安全状态。

## 所有权

任一发送 API 返回后，调用者即可复用存储。所有回调均同步借用缓冲区；即使使用 DMA，PHY TX 返回前也必须完成读取或复制。此接口减少组帧复制，驱动仍可能需要复制到自有 DMA 缓冲。不得保存借用指针，也不得传入只读存储。

## 失败处理

检查返回值。本地成功不能证明远端交付，失败也可能已经修改头部与尾部。认证发送一旦预留安全序号，后续 provider 或 PHY 失败不会回退该序号；重试应再次调用 API，不能重放之前序列化的帧。详见[安全机制](../protocol/security.md)。

调用、查询与重试均遵循实例串行化规则。布局查询和基础描述符校验阶段的拒绝不构成发送尝试；进入统一 network 发送路径后的错误按[统计契约](../reference/statistics.md)计数，不能只凭返回错误码判断失败阶段。
