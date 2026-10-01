# 静态分析

## 执行

先按[构建与测试](../getting-started/build-and-test.md)准备五个基础包和 GoogleTest/GoogleMock，再显式安装 `tools/quality.json` 固定提交的 xgen-quality 包。工具版本由该共享包管理，缺失或不匹配都会失败。

```sh
cmake --preset gcc-test \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset gcc-test
python tools/quality.py cppcheck --build-dir build/gcc-test
python tools/quality.py tidy --build-dir build/gcc-test
```

`xgl_static_analysis` 调用同一个共享 `cppcheck` 入口，可使用 `cmake --build build/gcc-test --target xgl_static_analysis`。该 target 使用配置时发现的 Python 解释器，因此应在已安装质量包的 Python 环境中配置；必要时显式设置 `Python3_EXECUTABLE`。Clang-Tidy 通过上述 `tidy` 命令执行。

源码开发在已配置的 dev 构建目录运行相同入口，例如 `--build-dir build/dev-debug`。正式模块作为子目录时默认关闭分析辅助；需要时显式设置 `XGL_BUILD_STATIC_ANALYSIS_TARGET=ON`。生产构建不会安装质量工具。

## 输入与诊断

共享 runner 从实际 `compile_commands.json` 选择 `tools/quality.json` 声明的生产源码，保留工具链、宏、include 和 profile。Cppcheck 执行共享策略中的 warning、performance、portability 检查；Clang-Tidy 使用根 `.clang-tidy`，当前启用 `clang-analyzer-*`、`bugprone-*`、`performance-*`。不要用独立手写 include 列表替代编译数据库。

固定工具及可选路径环境变量由质量包管理：`XGEN_CPPCHECK`、`XGEN_CLANG_TIDY`。它们仅选择本地可执行文件，仍执行版本核验。编译数据库不存在、生产输入为空、工具失败或有阻断诊断，均非零退出。结果及选中源码保存到 `out/reports/`；本地、CMake 与 CI 共用这些判定。

公共 API 注释另外通过 `python tools/quality.py docs` 检查，根 `Doxyfile` 要求缺文档、参数和文档错误失败；文档站点继承同一配置生成 HTML。格式检查使用 `python tools/quality.py format`，不与静态分析混成一个模糊结论。

## 边界

分析只覆盖实际编入的配置。Full、Embedded、Boot 及不同工具链分别记录；未编入代码不自动获得通过结论。静态分析补充运行时、容量和 wire 测试，不证明密码算法恒定时间、中断安全、DMA 生命周期或完整调用链的最坏栈上界。
