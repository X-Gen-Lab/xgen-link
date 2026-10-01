# 独立开发装配

`dev/` 是协议仓库自己的开发入口。生产集成使用根 CMake 入口，由产品提供组件 targets 或已安装包；开发入口显式组装五个独立源码仓库，供本仓测试、示例、安装消费及资源诊断使用。它不下载依赖、不猜测兄弟目录、不嵌套公共组件副本。

## 本地构建

先自行准备五个仓库和固定的 GoogleTest 1.16.0（包含 GoogleMock）。以下路径全部替换为已有的绝对路径：

```sh
cmake -S dev -B build/dev-full -G Ninja \
  -DXGL_DEV_STATUS_SOURCE_DIR=/work/xgen-status \
  -DXGL_DEV_BYTES_SOURCE_DIR=/work/xgen-bytes \
  -DXGL_DEV_CRC_SOURCE_DIR=/work/xgen-crc \
  -DXGL_DEV_MEMORY_SOURCE_DIR=/work/xgen-memory \
  -DXGL_DEV_CONTAINERS_SOURCE_DIR=/work/xgen-containers \
  -DGTest_ROOT=/work/deps/gtest \
  -DCMAKE_BUILD_TYPE=Debug -DXGL_BUILD_TESTS=ON
cmake --build build/dev-full --parallel
ctest --test-dir build/dev-full --output-on-failure --no-tests=error
```

Windows 可使用 `D:/work/...` 形式。需要选择编译器时显式设置 `CMAKE_C_COMPILER` 与 `CMAKE_CXX_COMPILER`，确保 GoogleTest 与消费者的编译器/运行库一致。缺少任意一个组件路径会在配置阶段立即失败，且不会通过安装包或旧目录替代缺少的源码输入。

开发默认选择 Full，非 Boot 的测试与示例默认打开；测试开关只使用 `XGL_BUILD_TESTS`。Boot 默认关闭 GoogleTest 和示例，并禁止 fallback malloc；Embedded 默认不启用 fallback malloc。文档默认关闭。no-heap smoke、footprint、static-analysis、SDK 消费以及 release-validation 辅助目标默认打开，各自仍可通过对应 `XGL_BUILD_*` 选项关闭。启用静态分析或文档目标时必须自行准备它们所需的工具。

```sh
cmake --build build/dev-full --target xgl_release_validation --parallel
```

开发构建中，协议二进制目录为 `build/dev-full/link`，依赖目录为 `build/dev-full/dependencies`；GoogleTest 可执行位于 `link/test/xgl_tests`。全量 CTest 从开发构建根目录运行。源码组件只创建一次，选择协议、示例及测试需要的能力并集；Boot 不引入 hash/libc allocator，开发入口也不额外编译 arena、tracking、ring-buffer 等无关能力。

## 开发预设

`dev/CMakePresets.json` 提供 debug、release、test、gcc-debug、gcc-test、ci、asan、boot、embedded 和 docs。五个源码路径从同名 `XGL_DEV_*_SOURCE_DIR` 环境变量读取，也可用命令行 `-D` 显式覆盖。二进制目录为 `build/dev-<preset>`。

```sh
cmake -S dev --preset gcc-test
cmake --build build/dev-gcc-test --parallel
ctest --test-dir build/dev-gcc-test --output-on-failure --no-tests=error
# 或进入 dev 后使用 cmake --build --preset gcc-test / ctest --preset gcc-test。
```

ASan 预设先构建全部所选目标，再运行对应 CTest；不能仅编译 GoogleTest 可执行却要求 CTest 运行未构建的 smoke 和示例。该预设明确关闭 SDK 安装消费测试，因为全局 sanitizer 编译/链接参数不属于导出的生产包接口；安装消费由 Full 和生产依赖契约测试独立验证。docs 预设不需要 GoogleTest，其站点位于 `build/dev-docs/link/docs/site`。

## CI 依赖输入

`dependencies.json` 只固定本仓开发验证使用的五个提交，不是产品版本清单，也不决定其他仓库的依赖版本。`.github/actions/setup-components` 显式 checkout 这些提交到忽略的 `out/deps/xgen-*`，随后验证 HEAD 与工作树干净状态。源仓库由以下 GitHub repository variables 提供：

`XGEN_STATUS_REPOSITORY`、`XGEN_BYTES_REPOSITORY`、`XGEN_CRC_REPOSITORY`、`XGEN_MEMORY_REPOSITORY`、`XGEN_CONTAINERS_REPOSITORY`。

值为真实 `owner/repository`。未提供、提交不存在或访问失败时 CI 明确失败；本仓不会构造猜测地址或自动选择新版本。首次启用 CI 前，应确保这些独立仓库已存在且固定提交已发布。`prepare_dependencies.py` 只校验/输出 checkout 输入及复核已准备源码，不执行下载。依赖更新通过修改显式提交并重新验证完成。

本地开发入口与 CI 输入校验回归：

```sh
python -m unittest discover -s dev/tests -v
```

`Dependencies.cmake` 的 `xgl_dev_provide_components()` 也供 Boot 资源诊断工程复用。调用前提供五个路径并明确 `XGL_PROFILE`、`XGL_BUILD_TESTS`、`XGL_ALLOW_FALLBACK_MALLOC`；它不会启用主机测试或改变编译工具链。
