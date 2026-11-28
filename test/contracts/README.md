# 构建与交付契约

各组从仓库根目录运行。先按所选任务准备 CMake 3.24 或以上、Ninja 和 Python。生产编译采用 C11，真实主机测试和独立 C++ 安装消费按需启用 C++17。

| 目录 | 验证内容 |
| --- | --- |
| `build/` | preset、档位、编译资源、生成头、Debug 与 Release 安装及固定 RTO |
| `sdk/` | 纯 C 最小生产树、父工程集成、安装搬移、实际头与库的来源、资源身份和能力拒绝 |
| `ctest/` | 发现政策、历史用例、严格执行证据、C++17 与种子回放 |

CI 在相应任务中调用这些脚本。[开发工作流](../../docs/zh/development/workflow.md)说明完整验证与证据。

## 独立安装消费

```sh
python test/contracts/sdk/dependency_contracts.py \
  --link-source . \
  --work-dir build/dependency-contracts
```

复制生产所需源码后，分别构建 Boot 和 Full。配置传入不可用的 C++ 编译器，核验仅启用 C。安装包移到新前缀，移除临时生产源码和构建目录，独立 C 和 C++ 工程重新编译、链接并运行。检查导出的路径、实际归档与公共头均属于该前缀。给出另一份 SDK 的搜索提示仍须拒绝空预期前缀。能力失败后的查找与资源身份失配各有回归。

在 Linux 上追加 `--deny-network` 复现 CI。该模式使用 `libseccomp.so.2`，子进程先证明 IPv4 和 IPv6 套接字创建收到 `EPERM`。配置、构建、安装和消费继承相同限制。

宿主快速安装 smoke 的 CMake 夹具在本组，复用 [C11 smoke 源](../smoke)。独立脚本按次保留命令与日志，可以在命令末尾指定 unittest 方法。
