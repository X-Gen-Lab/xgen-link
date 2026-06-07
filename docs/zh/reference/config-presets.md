# 配置预设

| Preset | 目标 | 认证 | 说明 |
| --- | --- | --- | --- |
| Tiny | 小 MCU | 默认关闭 | 最小资源占用 |
| Small | 小型应用 | 默认关闭 | 更大 buffer |
| Medium | 常规 MCU | 默认关闭 | 平衡吞吐和资源 |
| Large | Host/大 MCU | 默认关闭 | 更大窗口和 buffer |
| Production | 发布配置 | 默认开启 | 要求 auth provider |

生产发布应从 Production preset 开始，再按目标板资源收紧。

## 精确默认值

| Preset | ACK Timeout | Retry | Window | Max Frame | TX Pool | RX Buffer |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Tiny | 1000 ms | 3 | 2 | 128 | 1024 | 160 |
| Small | 1000 ms | 5 | 4 | 256 | 2048 | 288 |
| Medium | 1000 ms | 5 | 8 | 512 | 4096 | 544 |
| Large | 1000 ms | 7 | 16 | 1024 | 8192 | 1056 |
| Production | 1000 ms | 7 | 16 | 1024 | 8192 | 1056 |

所有 preset 默认关闭 compression/encryption。Production 默认 `auth_required=true`，`auth_key_id=1`。
