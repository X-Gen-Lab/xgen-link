# Kconfig 和构建配置

CMake 是本仓库唯一的配置生成入口。项目不提供另一条 Kconfig 到头文件的生成链路。

## 编译配置

将 `XGL_PROFILE` 设为 `boot`、`embedded` 或 `full`。CMake 生成 `generated/xgl/xgl_build_config.h`，包含能力、ABI 和 build 标识。使用导出的 `xgl::xgl` target 或安装 package，确保读取匹配的生成头文件。

生产镜像不允许 libc 分配时，设置 `XGL_ALLOW_FALLBACK_MALLOC=OFF` 和 `XGM_BUILD_LIBC_ALLOCATOR=OFF`。测试配置可能额外构建分配器支持，测量时须使用独立生产构建。

## 宿主 RTOS 映射

宿主 RTOS 可以提供自己的 Kconfig 选项，并在添加 xgen-link 前显式转换为 CMake cache 参数。例如宿主的 boot 选项映射到 `-DXGL_PROFILE=boot`。这段转换属于宿主构建，应在宿主侧验证。

不能只在应用某个源文件中定义能力宏，却链接不同 profile 的库。也不能认为 `.config` 会自动改变本仓库生成的头文件。

## 运行时配置

`xgl_config_t` 在编译能力上限内选择功能、路由、超时及固定资源容量。查询或准备 workspace 前先校验配置。运行时不能开启已被编译裁掉的能力。

字段含义见[配置](configuration.md)，存储计算见[资源模型](resource-model.md)。
