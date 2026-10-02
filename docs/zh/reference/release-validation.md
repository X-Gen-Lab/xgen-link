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

多配置生成器使用 `cmake --build <dir> --config Release --target xgl_release_validation`。目标将 Release 继续传给 CTest 和 SDK 的嵌套 build/install，Debug/Release 消费者在独立子目录中验证；直接运行 CTest 时仍需 `-C Release`。

源码开发按仓库 `dev/README.md` 提供五个源码路径，使用 `cmake -S dev --preset ci`，把以上构建目录替换为 `build/dev-ci`。生产子目录默认不添加这些辅助；产品按需显式开启检查选项。dev 清单只固定协议仓库的测试输入。

## 配置矩阵

```sh
cmake --preset boot-host-test -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset boot-host-test
python tools/quality.py test --build-dir build/boot-host-test --config MinSizeRel
cmake --preset embedded-lite-host-test -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset embedded-lite-host-test
python tools/quality.py test --build-dir build/embedded-lite-host-test --config MinSizeRel
cmake --preset embedded-host-test -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset embedded-host-test
python tools/quality.py test --build-dir build/embedded-host-test --config MinSizeRel
```

以上为原生主机配置，须先准备兼容的 GoogleTest/GoogleMock 1.16.0（用 `GTest_ROOT` 或 `GTest_DIR` 指定）及测试所需的基础包能力。Boot/Lite/Embedded 分别构建，不启用协议 libc fallback。Boot/Lite 验证各自可用能力，Embedded 执行完整回归。源码复用、安装消费、能力/依赖拒绝、Debug/Release 发布以及 C++17/测试回放契约由 `test/cmake` 脚本验证；CI 固定运行这些任务。

原生 Host、ASan/UBSan、受限配置、安装消费和覆盖率按实际环境分别保留报告。CI 的任务与固定准备流程见 [GitHub CI](github-ci.md)。发布记录应包含源码/依赖提交、工具链、配置和执行结果；远端通过需要对应目标提交的实际运行。

## 源码基线与安装 API 证据

发布基线记录协议与五个基础组件的源码提交、包版本/ABI、工具链、编译 profile/resource 身份、配置及报告摘要。产品另外记录板级适配和认证 provider。`dev/dependencies.json` 固定 link 开发输入，不替代产品依赖选择。只有取得对应的已发布源码引用及真实远端检查结果，才能称为已发布 SDK 基线；在此之前，工作区结果仅作为本地发布候选。

安装 C11 smoke 只通过 `<xgl/xgl.h>` 和 `xgl::xgl` 构建两个消费者：静态生命周期/背压收发，以及使用显式存储的 API 消费者。后者覆盖 `xgl_create_with_error()`、配置大小/ABI/build 身份拒绝、分配失败和 `xgl_next_timeout_for_work()`，并验证查询不轮询 PHY、关闭 RX 后不包含 PHY 轮询期限。消费者不依赖 GoogleTest 或私有头；已有主机 SDK smoke 任务执行这两个程序。

## 产品验收

单独运行 Cortex-M0 最终 ELF 资源探针，再接入真实板卡。核算完整镜像的 Flash、静态 RAM、栈、PHY 缓冲和实际 Boot 分区，验证 Flash 擦除/编程时间及断电恢复。认证需要生产 provider，以及持久化新鲜性状态或全新可信密钥。主机模拟、SDK 消费和 ELF 链接均按各自范围报告，板级验收见[验证矩阵](validation-matrix.md)。

CI 用锁定的 Arm GNU 15.2.rel1/GCC 15.2.1，分别构建 MTU/RX 128 和 64，检查对应资源 policy，输出 Flash、workspace、静态 RAM、预留栈和最大单函数栈的差异。堆符号、未解析符号、错误配置和缺失测量均失败。资源阈值只约束固定探针，不能作为板级实时性或调用链总栈的证明。新增工作流必须取得本次提交的远端结果后才可记为 CI 通过。

Boot ELF 探针链接固定的通用消费者，不测量所有公开 API 组合；其 PHY 为桩实现，启动代码不是板级 BSP。缺少目标硬件时，可记录 Flash/RAM/链接结果，但累计调用链栈、ISR 嵌套、驱动/DMA 时序、Flash 断电行为和生产密码实现仍须保留为待验收。固定栈预留及主机通过不能替代这些门槛。
