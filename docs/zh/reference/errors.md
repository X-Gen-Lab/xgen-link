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
