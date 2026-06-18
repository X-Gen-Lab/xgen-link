# 发布验证

发布候选必须从干净构建目录验证。

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_release_validation
ctest --preset gcc-test --output-on-failure
```

## Gate

- 单元、属性和集成测试通过。
- SDK consumer smoke 通过。
- noheap smoke 通过。
- footprint report 生成。
- cppcheck 已安装并通过。
- 文档构建在 `XGL_BUILD_DOCS=ON` 时通过。
- 工作区只允许明确接受的用户变更。

## 文档 Gate

```sh
mkdocs build --strict
cmake --build build/ci --target xgl_docs
```

`xgl_docs` 同时验证 MkDocs 双语站点和 Doxygen 公共 API。断链、缺页、Doxygen 配置错误都应阻止发布。

## 推荐顺序

1. 从干净 build 目录配置。
2. 构建 `xgl_tests`。
3. 运行 CTest。
4. 构建 `xgl_release_validation`。
5. 构建 `xgl_docs`。
6. 检查 `git status --short`。

## Wire Format 兼容性承诺

| 承诺 | 说明 |
| --- | --- |
| 基础头布局 | 24-byte 固定布局不变，所有版本兼容 |
| 字节序 | 始终 little-endian，不改变 |
| Magic 值 | `A5 5A` 不变 |
| Version 字段 | 当前固定 `2`，未来大版本升级时递增 |
| TLV 扩展 | 向后兼容：新扩展不影响旧接收端（忽略未知 TLV） |
| Flags 保留位 | 未使用位（`0xC0`）保留，不用于新功能 |
| Packet Type 保留值 | 4–7 为预留，未来使用时旧版本应忽略 |

## API 稳定性承诺

| 层级 | 稳定性 | 说明 |
| --- | --- | --- |
| `xgl.h` 公共 API | Stable | 语义兼容，不移除或改名已有函数 |
| `xgl_types.h` 类型 | Stable | 不移除已有字段，可追加新字段 |
| `xgl_config.h` 配置 | Stable | 不移除已有配置项，可追加新项 |
| `xgl_error.h` 错误码 | Stable | 不重新编号已有错误码 |
| `include/xgl/internal/` | Internal | 不作为稳定 ABI，可自由修改 |

## 版本升级策略

- **Minor 版本**：新增功能、新扩展、新 Kconfig 选项，不破坏已有 API 或 wire format
- **Major 版本**：可修改 wire format 基础头、移除已废弃 API，需文档标注迁移路径
