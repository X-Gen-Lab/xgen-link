# 静态分析

编译基线：

- `-Wall`
- `-Wextra`
- `-Werror`
- `-Wpedantic`
- conversion/sign/cast 相关警告

cppcheck 基线：

```sh
cmake --build build/gcc-test --target xgl_static_analysis
```

发布环境必须安装 cppcheck。不可把 unavailable 当作 release 通过。

重点审查：

- v2 wire header offset 编解码。
- allocator 失败路径。
- fragment reassembly 生命周期。
- callback reentrancy。
- ISR 到协议 task 的边界。
