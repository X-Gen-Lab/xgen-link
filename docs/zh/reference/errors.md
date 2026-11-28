---
id: reference/errors
type: reference
audience: user
source_language: zh_CN
---

# 错误

`xgl_error_t` 定义公开协议错误。公共入口在出错时返回本表的状态；私有资源操作的失败由相应的协议边界转换。

## 处理

- `WINDOW_FULL`、`QUEUE_FULL`、`BUSY`、`NO_MEMORY`：施加背压；发送失败后不能假定所有权已被接纳。
- `INVALID_FRAME`、`CRC_FAILED`、`INVALID_VERSION`：拒绝该帧；认证失败作为无效帧报告。
- `ACK_TIMEOUT`：精确 peer scope 进入失败终态，重新连接必须使用新 epoch。
- `CANCELLED`：显式关闭或 RESET 终止了待发送操作。
- `UNSUPPORTED`：所选 profile 或 API 路径不提供该能力。

接收接纳回调在接受数据前返回 `BUSY`；transport 不确认无法保留或交付的数据。完成回调不得重入实例。参见 `test/property/test_error_properties.cpp`。

错误回调的 `message` 始终非空指针。Boot 关闭 `XGL_FEATURE_DIAGNOSTICS` 时传入空字符串以裁掉详细诊断文本；错误码和回调时机保留，应用应按错误码决策。
