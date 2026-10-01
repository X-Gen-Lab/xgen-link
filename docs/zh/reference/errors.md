# 错误

`xgl_error_t` 是协议错误域。通用组件状态由 xgen-status 的 `xgs_status_t` 定义；协议在模块边界转换通用组件的失败结果。

## 处理

- `WINDOW_FULL`、`QUEUE_FULL`、`BUSY`、`NO_MEMORY`：施加背压；发送失败后不能假定所有权已被接纳。
- `INVALID_FRAME`、`CRC_FAILED`、`INVALID_VERSION`：拒绝该帧；认证失败作为无效帧报告。
- `ACK_TIMEOUT`：精确 peer scope 进入失败终态，重新连接必须使用新 epoch。
- `CANCELLED`：显式关闭或 RESET 终止了待发送操作。
- `UNSUPPORTED`：所选 profile 或 API 路径不提供该能力。

接收接纳回调在接受数据前返回 `BUSY`；transport 不确认无法保留或交付的数据。完成回调不得重入实例。参见 `test/property/test_error_properties.cpp`。
