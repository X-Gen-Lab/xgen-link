# 构建与消费

生产库由 C11 编译器和 CMake 3.24 或以上构建。从 xgen-link 仓库根目录执行下列命令。默认构建只生成静态库和匹配的配置头。

```sh
cmake -S . -B build/production
cmake --build build/production
cmake --install build/production --prefix "$PWD/build/sdk"
```

检查安装前缀中的公开头、静态库和 `xglConfig.cmake`。保留库与同一产物的生成头。不同编译器、目标 ABI、profile 或资源组合使用独立构建目录和前缀。多配置生成器按同一个 `--config` 构建和安装，Debug 与 Release 分别安装到各自前缀。

## CMake 消费

从源代码装配时，父工程先确定工具链和所需的 [编译档位](../reference/profiles.md)，再调用 `add_subdirectory(path/to/xgen-link)`。安装消费使用以下入口。两种入口均提供 `xgl::xgl`。

```cmake
find_package(xgl 4 CONFIG REQUIRED COMPONENTS wire reliable)
target_link_libraries(my_application PRIVATE xgl::xgl)
```

在消费工程配置时通过 `CMAKE_PREFIX_PATH` 指定 SDK 前缀。安装包按编译能力选择 `COMPONENTS`，其中 `wire` 和 `reliable` 适用于所有档位。成功配置后再编译并运行实际消费者。

## 编译资源选项

裸机 Boot 示例配置使用 `cmake --preset boot`，生成器为 Ninja。若同一镜像需要固定 RTO 和编译窗口 1，使用独立构建目录并显式选择资源：

```sh
cmake -S . -B build/boot-small -G Ninja -DXGL_PROFILE=boot \
  -DXGL_TRANSPORT_WINDOW_CAPACITY=1 -DXGL_RELIABLE_INDEX_BUCKETS=1 \
  -DXGL_ENABLE_ADAPTIVE_RTO=OFF
cmake --build build/boot-small
```

资源身份及窗口、可靠索引桶、读取块和扩展运行时选项见[编译档位](../reference/profiles.md)。目标板还需选定实际交叉工具链及应用容量。

## 主机验证

显式准备包含 GoogleMock 的 GoogleTest 1.16.0，并使用新的原生构建目录。配置阶段使用已有工具和安装包。

```sh
cmake --preset gcc-test -DGTest_DIR=/path/to/gtest/lib/cmake/GTest
cmake --build --preset gcc-test
ctest --preset gcc-test --no-tests=error
```

使用 `boot-host-test`、`embedded-lite-host-test` 或 `embedded-host-test` 验证对应能力。C11 示例、无堆检查、文档、质量及发布辅助各自显式开启。完整验证流程见[开发工作流](../development/workflow.md)。
