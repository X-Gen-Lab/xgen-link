# 公共 API

普通 SDK 用户只依赖安装的公共头：

- `xgl.h`
- `xgl_config.h`
- `xgl_types.h`
- `xgl_error.h`

内部协议头如 `xgl_wire.h`、`xgl_parser.h`、`xgl_reliable.h` 不属于普通用户 API。

## API 分组

- 生命周期：`xgl_create`、`xgl_init`、`xgl_destroy`
- 发送：`xgl_send`、`xgl_send_zerocopy`
- 运行：`xgl_run`、`xgl_next_deadline_ms`
- 统计：`xgl_stats_get`、`xgl_stats_reset`
- 版本：`xgl_version_string`、`xgl_version_int`

## Doxygen

CMake 文档构建会生成公共 C API 参考：

<a href="../../../api/doxygen/html/index.html">Open generated Doxygen API</a>
