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

## 失败处理

| 失败 | 处理 |
| --- | --- |
| compiler warning 被 `-Werror` 提升 | 修复代码，或把 conversion/cast 写得明确 |
| cppcheck 在 production code 中发现问题 | 修复代码；只有确认是 false positive 时才加定向 suppression |
| cppcheck 不可用 | 对 release validation 视为环境失败 |
| generated/build output warning | 优先从分析输入中排除生成产物，而不是 suppress 源文件 |

suppression 必须尽量窄：优先使用 line-level 或 symbol-level suppression，并在代码附近写明简短原因。不要对 wire format、allocator lifetime、authentication 或 fragment reassembly 路径添加宽泛的整文件 suppression。

重点审查：

- v2 wire header offset 编解码。
- allocator 失败路径。
- fragment reassembly 生命周期。
- callback reentrancy。
- ISR 到协议 task 的边界。
