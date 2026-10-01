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

## 编译资源选项

产品在编译协议库时选择以下资源组合，正式根工程和 `dev` 装配接受相同选项。这些选项确定实际对象布局和行为，消费者不能通过自己的宏定义改变已安装库。

| CMake 选项 | 支持值与默认值 | 作用 |
| --- | --- | --- |
| `XGL_RELIABLE_INDEX_BUCKETS` | `AUTO`、`1`、`2`、`4`、`8`、`16`、`32`；默认 `AUTO` | 每个 peer 的可靠队列索引桶数；少量桶减少固定存储，较长桶链可能增加查找成本 |
| `XGL_TRANSPORT_WINDOW_CAPACITY` | `AUTO`、`1`、`8`、`16`、`32`、`64`、`128`；默认 `AUTO`，解析为 `128` | 私有窗口及其内嵌确认位图的编译容量 |
| `XGL_ENABLE_ADAPTIVE_RTO` | `ON`、`OFF`；默认 `ON` | 是否保留 peer RTT 估计器与 ACK 采样；`OFF` 使用配置或请求指定的 ACK 超时 |

索引桶的 `AUTO` 默认值为 Boot `1`、Embedded Lite `8`、Embedded/Full `32`。编译窗口容量与运行时 `protocol.window_size` 分别选择：公开运行时窗口仍为 `1–32`，且不能超过编译容量；默认私有 `128` 槽能力保持历史契约，不会扩大公开 API 的窗口上限。关闭自适应 RTO 不取消单次超时覆盖或重传指数退避，详见[可靠传输](../protocol/reliability.md)。

例如，在五个依赖包已安装后构建一个单包窗口、固定 RTO 的 Boot SDK：

```sh
cmake -S . -B build/boot-compact -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/foundation-sdk \
  -DXGL_PROFILE=boot \
  -DXGL_RELIABLE_INDEX_BUCKETS=1 \
  -DXGL_TRANSPORT_WINDOW_CAPACITY=1 \
  -DXGL_ENABLE_ADAPTIVE_RTO=OFF \
  -DXGL_BUILD_TESTS=OFF -DXGL_BUILD_EXAMPLES=OFF
cmake --build build/boot-compact
cmake --install build/boot-compact --prefix ./install-boot-compact
```

该命令仅选择构建组合，不证明镜像满足某个 Flash、RAM 或栈预算。实际产品还须设置运行时容量，并验收完整目标 ELF 与板级峰值。支持的资源配置、窗口边界、冲突拒绝和安装消费由 `test/cmake/test_transport_resource_config.py` 验证；固定 RTO 行为由 `test/unit/transport/test_transport_fixed_rto.cpp` 验证。

## 构建身份与 SDK 一致性

全部资源保持 profile 默认值时，`XGL_BUILD_CONFIG_ID` 保持 Boot `301`、Embedded `302`、Full `303`、Embedded Lite `304`。实际组合偏离默认值时会生成不同身份，包含索引桶、窗口容量、自适应 RTO、已解析的 PHY 读取块大小及 libc fallback 选择。`XGL_DATALINK_RX_CHUNK_SIZE` 支持 `AUTO/16/32/64/128`，`AUTO` 在 Boot 解析为 `32`、其余 profile 为 `128`；libc fallback 默认为 Full 开启、其余关闭，Boot 要求关闭。

生成头同时提供编译资源身份；CMake `xgl::xgl` 导出对应属性，安装包提供 `xgl_BUILD_CONFIG_ID`、`xgl_RESOURCE_CONFIG_ID` 和已解析的资源选项。安装配置拒绝 package 与既有 target 身份不符，目标的兼容接口拒绝同一消费者组合冲突，生成头拒绝与已编译资源冲突的宏覆盖，公开 checked 入口拒绝调用方与运行库身份不匹配。消费者应使用同一 SDK 的 target 和生成头，并通过生成身份调用公开入口，不能只按 profile 名称硬编码 `301–304`。

重新选择资源组合时使用独立构建目录，安装到独立 SDK 前缀，随后重新编译消费者。对已安装包设置这些 CMake 选项不会重新裁剪库；必须重新构建并选择匹配的 SDK。[内存与容量](../protocol/memory.md)说明如何查询该构建的实际工作区需求。

## 安装

```sh
cmake --install build/gcc-test --prefix ./install
```

五个依赖包需分别安装到同一前缀或由 `CMAKE_PREFIX_PATH` 指定的多个前缀；安装 link 不代替安装依赖。消费者使用 `find_package(xgl CONFIG REQUIRED)` 和 `xgl::xgl`，安装配置解析实际所需组件并检查版本/ABI。使用安装包生成的 profile 与资源配置头文件；checked 入口拒绝 ABI/profile/编译资源身份不匹配。

消费者可要求 `find_package(xgl CONFIG REQUIRED COMPONENTS fragmentation out_of_order)`，在配置阶段拒绝能力不足的包；可用能力及 profile 身份见[配置](../guide/configuration.md)。

## 多配置生成器

Ninja Multi-Config 或 Visual Studio 构建需显式传递相同配置：

```sh
cmake --build build/multi --config Release --target xgl_release_validation
ctest --test-dir build/multi -C Release --output-on-failure --no-tests=error
cmake --install build/multi --config Release --prefix ./install-release
```

发布目标内部将配置传给 CTest、嵌套 SDK 构建及安装，并按配置隔离消费者目录。`test/cmake/test_profile_builds.py` 实际构建 Debug/Release 两种消费者并检查对应安装目标文件。
