# 构建和 SDK 契约

从仓库根目录运行以下测试。准备 CMake 3.24 或以上、Ninja、Python、C11 和 C++17 编译器。C++ 编译器只用于验证安装包消费。

```sh
python test/cmake/dependency_contracts.py \
  --link-source . \
  --work-dir build/dependency-contracts
```

测试将生产 CMake 文件、公共头和协议源码复制到独立临时目录，禁用包注册表、五个原基础组件包发现和 FetchContent 获取与更新，分别编译 Boot 与 Full。生产配置传入不可用的 C++ 编译器，以核验只启用 C。库安装后移动到新的前缀，删除这轮临时源码与生产构建目录，由独立 C 和 C++ 工程消费。测试检查安装头文件集合、导出目标的链接依赖，验证 CMake 目标和头文件 ABI 不一致、旧版 SDK、运行时错误 ABI 和配置尺寸、Boot 请求认证能力均被拒绝，并覆盖父工程集成。

在 Linux 上追加 `--deny-network` 可复现 CI 的禁网验证。该模式需要 `libseccomp.so.2`（Ubuntu 的 `libseccomp2`），以普通用户运行。配置、构建、安装和 SDK 消费的所有子进程继承过滤器。测试日志记录独立子进程创建 IPv4 和 IPv6 套接字时收到 `EPERM`。

脚本按次在输出目录保存命令与日志。可在命令末尾追加 unittest 的方法名，例如 `StandaloneContracts.test_subdirectory_defaults_do_not_add_developer_targets`。
