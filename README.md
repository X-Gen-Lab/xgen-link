# xgen-link

[中文文档](docs/zh/index.md) · [English documentation](docs/en/index.md)

XGen Link 是独立的 C11 嵌入式通信协议库。它提供 Wire v1、可靠单播、有界工作区和可选认证。产品提供 PHY、单调时间、执行者与可信密钥。

XGen Link is a standalone C11 communication library. It provides Wire v1, reliable unicast, bounded workspaces and optional authentication. The product supplies the PHY, monotonic time, execution context and trusted keys.

## 构建 / Build

在仓库根目录用 CMake 3.24 或以上和 C11 编译器构建。Run from the repository root with CMake 3.24 or later and a C11 compiler.

```sh
cmake -S . -B build/production
cmake --build build/production
cmake --install build/production --prefix "$PWD/build/sdk"
```

应用可用源码子目录或安装包消费 `xgl::xgl`。构建档位、消费命令和可选主机测试见[构建与消费](docs/zh/guides/build-and-consume.md)。

Use a source subdirectory or an installed SDK to consume `xgl::xgl`. See [Build and consume](docs/en/guides/build-and-consume.md) for profiles, consumer commands and optional host tests.

## 使用 / Use

- [跑通首次收发](docs/zh/tutorials/first-link.md) · [Run your first link](docs/en/tutorials/first-link.md)
- [架构与资源所有权](docs/zh/explanation/architecture.md) · [Architecture and ownership](docs/en/explanation/architecture.md)
- [Boot 接入](docs/zh/guides/boot.md) · [Integrate Boot](docs/en/guides/boot.md)
- [SDK 5 迁移](docs/zh/guides/migration.md) · [Migrate to SDK 5](docs/en/guides/migration.md)
- [贡献指南](CONTRIBUTING.md) · [Engineering conventions](docs/standards.md)
