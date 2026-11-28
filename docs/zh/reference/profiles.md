---
id: reference/profiles
type: reference
audience: user
source_language: zh_CN
---

# 编译档位与实例容量

profile 定义编译能力上限。应用在该范围内填写每个实例的容量。不支持的能力由配置校验拒绝。

## 编译档位

| 档位 | 认证 | 转发 / 乱序 | libc 默认值 |
| --- | --- | --- | --- |
| boot | 不支持 | 不支持 | 关闭 |
| embedded-lite | 不支持 | 乱序 | 关闭 |
| embedded | 支持，4 个会话槽 | 支持 | 关闭 |
| full | 支持，16 个会话槽 | 支持 | 开启 |

## 编译资源身份

`XGL_TRANSPORT_WINDOW_CAPACITY` 可选 1、8、16、32、64、128，默认 128。`XGL_RELIABLE_INDEX_BUCKETS` 可选 1、2、4、8、16、32，按 profile 默认为 Boot 1、Embedded Lite 8、其余 32。`XGL_DATALINK_RX_CHUNK_SIZE` 可选 16、32、64、128 字节，默认 Boot 32、其余 128。自适应 RTO 默认开启；高级运行时默认关闭。

`XGL_DATALINK_MAX_FRAME_SIZE` 是生产者选择的完整帧上限，默认 2048 字节，可选 64–2048 字节。实例 MTU 必须适应该上限。生成头与安装目标记录 档位及编译资源标识。消费时检查实际导出，避免用消费者宏覆盖已选择的资源。Boot 要求关闭 libc fallback。

## 运行容量

应用根据链路 MTU、同时通信的对端数量和允许的在途包数设置实例容量。Boot 使用单包窗口，乱序缓冲容量可以为零。启用认证的应用还须提供认证服务并安装可信会话。字段约束和 C11 初始化示例见[配置指南](../guides/configuration.md)。
