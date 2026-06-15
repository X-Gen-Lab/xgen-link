# 配置

配置从 `xgl_config_t` 开始，推荐先使用 preset，再覆盖目标板差异。

## 必填项

- `source_id`：本地 16-bit 节点 ID。
- `route_table` 和 `route_table_len`：至少提供可达目标的 PHY route。
- `rx_callback`：需要接收应用 payload 时设置。

## Route Item

| 字段 | 说明 |
| --- | --- |
| `target_id` | 目标节点 ID |
| `phy` | TX/RX callback 集合 |
| `max_frame_size` | 当前 route 可承载的完整 frame 长度 |
| `read_freq_hz` | 该 PHY 的轮询频率 |
| `metric` | route 选择和替换的代价指标 |

`max_frame_size` 必须大于基础头、扩展、payload、auth trailer 和 CRC 的总和。

## 认证

生产 preset 启用 `auth_required`。此时必须设置：

- `auth_key_id`
- `auth_provider`
- `auth_provider->tag_len`，必须满足 `0 < tag_len <= XGL_AUTH_TAG_MAX_LEN`

## Memory

`memory.allocator` 控制协议内部内存来源。严格 MCU profile 应关闭 fallback malloc，并在 init 阶段提供足够池化资源。

| 字段 | 说明 |
| --- | --- |
| `tx_pool_size` | TX 池预算 |
| `rx_buffer_size` | parser/RX cache 预算 |
| `allocator` | 自定义 allocator，NULL 行为由 `XGL_ALLOW_FALLBACK_MALLOC` 控制 |

## Feature Flags

压缩和加密当前为 reserved。生产配置不应启用未接入 payload expansion/security model 的 codec。

## 校验建议

- `source_id` 不得为 0 或保留地址。
- `window_size` 不应超过可靠队列和 MCU RAM 预算。
- `max_retry_count` 与低功耗 sleep 周期一起评估。
- production preset 必须提供 auth provider 后再 create/init。
