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
