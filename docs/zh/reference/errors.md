# 错误码

| 范围 | 示例 | 含义 |
| --- | --- | --- |
| 0 | `XGL_OK` | 成功 |
| 1-99 | `XGL_ERR_INVALID_PARAM` | 参数错误 |
| 100-199 | `XGL_ERR_NO_MEMORY` | 内存或 buffer 错误 |
| 200-299 | `XGL_ERR_ROUTE_NOT_FOUND` | 网络和传输错误 |
| 300-399 | `XGL_ERR_INVALID_FRAME` | 协议帧错误 |
| 400-499 | `XGL_ERR_QUEUE_FULL` | 状态或队列错误 |

推荐使用 `xgl_error_string()` 转换为日志文本。

## 错误传播策略

### 各层错误处理规则

| 层 | 错误类型 | 策略 | 说明 |
| --- | --- | --- | --- |
| Wire | CRC 错误 | fail-closed | 丢弃帧，不交付上层 |
| Wire | header 字段非法 | fail-closed | 丢弃帧 |
| Datalink | 认证失败 | fail-closed | 丢弃帧，报告 error_callback |
| Datalink | 重放拒绝 | fail-closed | 丢弃帧，计数 |
| Network | 路由未找到 | fail-closed | 丢弃 packet，报告错误 |
| Network | TTL 过期 | fail-closed | 丢弃 packet |
| Network | MTU 超限 | fail-closed | 丢弃 packet |
| Transport | reliable queue 满 | 丢弃 | 新 packet 被丢弃 |
| Transport | peer state 未找到 | 创建 | 首次看到新 peer 时自动创建 |
| Transport | 超时重传 | 重试 | retry_count++ 直到 max_retry_count |
| Transport | max_retry 超限 | fail-closed | 报告 error_callback |
| Memory | 分配失败 | 返回 NULL | 上层检查后决定降级或拒绝 |

### Error Callback 线程安全

- `xgl_error_callback_t` 在 `xgl_run()` 上下文中调用
- `XGL_THREAD_SAFE` 模式下，error_callback 可能从任意 `xgl_run()` 线程调用
- 回调函数内不应执行长时间阻塞操作
- 回调函数内不应调用 `xgl_send()`（可能死锁）

### 错误恢复路径

```text
参数错误 → 返回 xgl_error_t，不修改状态
帧解析错误 → 丢弃帧，parser 继续
认证失败 → 丢弃帧，error_callback
重传超限 → error_callback，peer state 标记不活跃
内存耗尽 → 返回 NULL，上层决定
```

### 可观测性

所有 fail-closed 路径都应通过 `xgl_statistics_t` 暴露计数器。生产调试时优先检查：

1. `rx_header_crc_errors` / `rx_crc16_errors`：物理层质量问题
2. 认证失败计数：密钥配置或攻击
3. 重放拒绝计数：网络环路或重传异常
4. reliable 重传计数：链路质量