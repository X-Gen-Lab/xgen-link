# 配置预设

profile 定义编译能力上限，preset 在上限内提供显式运行容量。不支持的能力由配置校验拒绝。

## 编译档位

| 档位 | 认证 | 分片 / 转发 / 乱序 | libc 默认值 |
| --- | --- | --- | --- |
| boot | 不支持 | 不支持 | 关闭 |
| embedded-lite | 不支持 | 分片、乱序 | 关闭 |
| embedded | 支持，4 个会话槽 | 支持 | 关闭 |
| full | 支持，16 个会话槽 | 支持 | 开启 |

## 编译资源身份

`XGL_TRANSPORT_WINDOW_CAPACITY` 可选 1、8、16、32、64、128，默认 128。`XGL_RELIABLE_INDEX_BUCKETS` 可选 1、2、4、8、16、32，按 profile 默认为 Boot 1、Embedded Lite 8、其余 32。`XGL_DATALINK_RX_CHUNK_SIZE` 可选 16、32、64、128 字节，默认 Boot 32、其余 128。自适应 RTO 默认开启；高级运行时默认关闭。

生成头与安装目标记录 profile、资源组合和构建身份。消费时检查实际导出，避免用消费者宏覆盖已选择的资源。Boot 要求关闭 libc fallback。

## 运行容量

| 预设 | MTU | 窗口 | Peer | 保留 TX | 保留 RX | 消息字节 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| boot | 128 | 1 | 1 | 1 | 0 | 禁用 |
| tiny | 128 | 2 | 1 | 2 | 2 | 禁用 |
| small | 256 | 4 | 2 | 4 | 4 | 1024 |
| medium | 512 | 8 | 8 | 16 | 16 | 4096 |
| large / production | 1024 | 16 | 16 | 64 | 64 | 16384 |

Production 还要求认证 provider，并在流量开始前显式安装可信会话。共享消息预算与重组槽限制同时生效；查阅 `xgl_feature_config_t`，使用 `xgl_memory_requirements()` 测量实际配置。预设名称不是实测 MCU RAM/Flash 保证。
