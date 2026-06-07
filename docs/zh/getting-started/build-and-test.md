# 构建与测试

## GCC 测试构建

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_tests
ctest --preset gcc-test --output-on-failure
```

## Release Gate

```sh
cmake --build build/gcc-test --target xgl_release_validation
```

该目标聚合单元测试、SDK consumer smoke、noheap smoke、footprint 和 cppcheck 静态分析。

## 文档构建

```sh
mkdocs build --strict
cmake --preset ci
cmake --build build/ci --target xgl_docs
```

`xgl_docs` 会生成 Doxygen 公共 API，再构建 MkDocs 双语站点。

## 常见问题

- Windows 下 GCC preset 需要 `gcc` 和 `g++` 可在 PATH 中找到。
- 文档构建失败通常是依赖未安装、链接断裂或 Doxygen 不在 PATH。
- release 环境必须安装 `cppcheck`，不能把静态分析 unavailable 作为通过条件。
