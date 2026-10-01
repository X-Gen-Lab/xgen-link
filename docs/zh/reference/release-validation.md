# 发布验证

## 准备输入

先按[构建与测试](../getting-started/build-and-test.md)安装五个基础包，并准备 GoogleTest/GoogleMock 1.16.0、`tools/quality.json` 固定提交的 xgen-quality 包和 `docs/requirements.txt` 中的文档依赖。使用质量包要求的 Cppcheck、Clang-Tidy 和 Doxygen；工具缺失或版本不符应先修复准备步骤。

以下根工程命令只消费安装包。`ci` 预设为原生 GNU、Debug、Full，并启用生产 `xgl` 覆盖率。覆盖率要求独立构建目录，避免沿用其他源码或配置的计数。

## 本地门禁

```sh
python tools/quality.py text
python tools/quality.py format
python -m pre_commit run --all-files
cmake --preset ci \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset ci --parallel 2
python tools/quality.py test --build-dir build/ci
python tools/quality.py tidy --build-dir build/ci
python tools/quality.py docs
python tools/quality.py coverage --build-dir build/ci --gcov-executable gcov
cmake --build build/ci --target xgl_release_validation --parallel 2
pwsh -File tools/docs_qa.ps1
```

共享测试入口校验 CTest 清单与实际 JUnit；行、函数、分支覆盖率分别至少 80%。报告保存在 `out/reports/`。新增或修复行为应同时提供 TDD 的 RED/GREEN 记录；补充历史覆盖和纯文档修改使用适当的验证。

`xgl_release_validation` 构建并运行协议测试、示例、SDK 消费者、静态工作区与适用的 noheap smoke，依赖共享 Cppcheck target、资源报告和文档站点。上面的独立 runner 命令另外执行文本、Clang-Tidy、严格 API 文档和覆盖率门禁。`tools/docs_qa.ps1` 检查双语结构和过时 API 引用。

源码开发按仓库 `dev/README.md` 提供五个源码路径，使用 `cmake -S dev --preset ci`，把以上构建目录替换为 `build/dev-ci`。生产子目录默认不添加这些辅助；产品按需显式开启检查选项。dev 清单只固定协议仓库的测试输入。

## 配置矩阵

```sh
cmake --preset boot -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset boot
python tools/quality.py test --build-dir build/boot --config MinSizeRel
cmake --preset embedded -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset embedded
python tools/quality.py test --build-dir build/embedded --config MinSizeRel
```

Boot/Embedded 分别构建，不启用协议 libc fallback。Boot 只验收其选定的最小协议能力；完整 Embedded 协议回归需要另建明确启用 GoogleTest 的主机配置。源码复用、安装消费、错误依赖拒绝和 C++17/测试回放契约分别由 `test/cmake` 脚本验证。

原生 Host、ASan/UBSan、受限配置、安装消费和覆盖率按实际环境分别保留报告。CI 的任务与固定准备流程见 [GitHub CI](github-ci.md)。发布记录应包含源码/依赖提交、工具链、配置和执行结果；远端通过需要对应目标提交的实际运行。

## 产品验收

单独运行 Cortex-M0 最终 ELF 资源探针，再接入真实板卡。核算完整镜像的 Flash、静态 RAM、栈、PHY 缓冲和实际 Boot 分区，验证 Flash 擦除/编程时间及断电恢复。认证需要生产 provider，以及持久化新鲜性状态或全新可信密钥。主机模拟、SDK 消费和 ELF 链接均按各自范围报告，板级验收见[验证矩阵](validation-matrix.md)。
