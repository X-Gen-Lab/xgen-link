# 移植

移植 XGL 需要提供 PHY、时间、内存和可选同步原语。

## PHY Contract

```c
xgl_error_t tx(const uint8_t* data, size_t len, void* user_data);
xgl_error_t rx(uint8_t* buffer, size_t* len, void* user_data);
```

`rx` 无数据时返回 `XGL_OK` 且 `*len == 0`。

## Allocator Contract

所有动态内存必须通过配置 allocator。严格 MCU profile 关闭 fallback malloc。

## ISR 边界

ISR 不运行协议栈。推荐 ISR 将数据放入 ring buffer，然后唤醒协议 task 或主循环。

## 平台示例

参考 `examples/platforms` 中的 bare-metal、FreeRTOS 和 Windows mock port。
