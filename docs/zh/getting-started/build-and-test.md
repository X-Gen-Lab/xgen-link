# 构建与测试

## 正式模块依赖

```sh
cmake --preset gcc-test \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
```

示例 SDK 前缀须事先安装 status、bytes、CRC、memory、containers 五包；GoogleTest 1.16.0 的安装须包含 GoogleMock。五包的版本与 target 列表见[模块化迁移](../guide/modular-migration.md)。正式根工程只接受安装 package 或父工程预提供的兼容 targets，不下载依赖，不选择源码检出，也没有默认 `external` 子模块。

产品在顶层提供每包唯一的一组 targets，然后 `add_subdirectory` 接入 link 并链接 `xgl::xgl`。协议不递归获取第二份生产依赖。作为子目录时，示例、smoke 和发布辅助默认关闭；需要这些开发检查时显式选择。

## 独立源码开发

```sh
cmake -S dev -B build/dev-full -G Ninja \
  -DXGL_DEV_STATUS_SOURCE_DIR=/path/to/xgen-status \
  -DXGL_DEV_BYTES_SOURCE_DIR=/path/to/xgen-bytes \
  -DXGL_DEV_CRC_SOURCE_DIR=/path/to/xgen-crc \
  -DXGL_DEV_MEMORY_SOURCE_DIR=/path/to/xgen-memory \
  -DXGL_DEV_CONTAINERS_SOURCE_DIR=/path/to/xgen-containers \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build build/dev-full
ctest --test-dir build/dev-full --output-on-failure
```

五个路径均须显式指定为绝对路径；编译器、架构和 GoogleTest 安装须兼容。开发装配先提供基础 targets，再消费同一个正式 link 模块。开发/CI 固定输入由 `dev/dependencies.json` 记录，产品不读取它选择版本。详细默认值、准备和多 profile 命令见仓库的 `dev/README.md`。

旧 `XGL_{STATUS,BYTES,CRC,MEMORY,CONTAINERS}_SOURCE_DIR` 生产选项已移除，使用时会报错；必须配置 `dev` 及其 `XGL_DEV_*` 参数，或先安装依赖后配置根目录。新入口使用新构建目录，不复用旧根工程缓存。

## Full 回归

```sh
cmake --preset gcc-test \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset gcc-test
ctest --preset gcc-test
```

以上使用已安装依赖；也可运行上一节的开发装配。生产库为 C11，协议 GoogleTest 使用严格 C++17，测试依赖不进入生产导出接口。

## 有界档位

```sh
cmake --preset boot -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset boot
ctest --preset boot
cmake --preset embedded-lite -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset embedded-lite
ctest --preset embedded-lite
cmake --preset embedded -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk
cmake --build --preset embedded
ctest --preset embedded
```

三者关闭协议的 libc fallback，默认验证静态生命周期及安装后的纯 C 消费者。需要主机行为回归时显式传入 `-DXGL_BUILD_TESTS=ON -DGTest_DIR=/path/to/gtest/lib/cmake/GTest`。Boot/Lite 按编译能力选择测试，Embedded 使用完整回归；Lite 保留分片/乱序并裁剪认证/转发。

源码开发时使用对应 dev 预设，提供五个路径环境变量或显式 `-D` 参数，例如 `cmake -S dev --preset boot -DXGL_BUILD_TESTS=ON`。各 profile 使用独立目录；不复用 Full 的缓存或生成头。开发入口会为主机测试补齐测试使用的 libc allocator target，但 Boot 协议仍不链接它。

## 安装

```sh
cmake --install build/gcc-test --prefix ./install
```

五个依赖包需分别安装到同一前缀或由 `CMAKE_PREFIX_PATH` 指定的多个前缀；安装 link 不代替安装依赖。消费者使用 `find_package(xgl CONFIG REQUIRED)` 和 `xgl::xgl`，安装配置解析实际所需组件并检查版本/ABI。使用安装包生成的 profile 头文件；checked 入口拒绝 ABI/profile 不匹配。

消费者可要求 `find_package(xgl CONFIG REQUIRED COMPONENTS fragmentation out_of_order)`，在配置阶段拒绝能力不足的包；可用能力及 profile 身份见[配置](../guide/configuration.md)。

## 多配置生成器

Ninja Multi-Config 或 Visual Studio 构建需显式传递相同配置：

```sh
cmake --build build/multi --config Release --target xgl_release_validation
ctest --test-dir build/multi -C Release --output-on-failure --no-tests=error
cmake --install build/multi --config Release --prefix ./install-release
```

发布目标内部将配置传给 CTest、嵌套 SDK 构建及安装，并按配置隔离消费者目录。`test/cmake/test_profile_builds.py` 实际构建 Debug/Release 两种消费者并检查对应安装目标文件。
