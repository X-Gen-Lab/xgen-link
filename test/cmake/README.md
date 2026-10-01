# 依赖装配构建契约

这些 Python unittest 使用真实 CMake 配置、生产静态库和纯 C 消费者，验证协议作为模块加入父工程时的依赖所有权。它们不替代协议 GoogleTest，也不下载任何依赖。

显式提供五个已准备的组件源码、协议源码和独立输出目录：

```sh
python test/cmake/dependency_contracts.py \
  --link-source /path/to/xgen-link \
  --status-source /path/to/xgen-status \
  --bytes-source /path/to/xgen-bytes \
  --crc-source /path/to/xgen-crc \
  --memory-source /path/to/xgen-memory \
  --containers-source /path/to/xgen-containers \
  --work-dir /path/to/contract-results \
  --c-compiler gcc --generator Ninja
```

脚本先在输出目录配置、构建并安装显式组件，随后执行十项契约。每次创建新的 `run-*` 子目录，保留生成工程、构建结果、命令日志和 `result.json`，不删除旧证据。

| 契约 | 期望 |
| --- | --- |
| 父工程能力并集 | 公共组件由父工程加载一次；协议与 app 复用真实 target，ring/arena 可同时使用 |
| 安装包入口 | 没有预提供 target 时使用显式安装包，不能选择嵌套源码副本 |
| 子目录默认值 | tests/examples/docs/noheap/SDK/release/static-analysis/footprint 默认关闭 |
| 旧源码选项 | 五项 `XGL_*_SOURCE_DIR` 返回明确迁移错误 |
| external 哨兵 | 放在嵌套依赖中的 CMake 代码不得被执行 |
| 缺少依赖 | 不隐式补源码或下载，配置失败 |
| 不完整 target 集 | 缺失组件且禁用包发现时，配置明确失败 |
| 不兼容身份 | 错误 ABI、越界版本、同包混合补丁版本都拒绝 |
| 错误 target 类型 | 不能用 INTERFACE target 冒充生产静态库 |
| 独立镜像 | Full/Boot 分别配置、构建和运行消费者，生成配置 ID 与 hash 依赖不同 |

正例编译和执行真实库。缺 target 与错误 target 类型负例只为配置诊断构造声明，不声称这些声明是可运行实现。临时 external 哨兵复制当前协议构建输入以隔离场景，不修改工作区或子模块。

可用 `--case test_subdirectory_defaults_do_not_add_developer_targets` 单独选择用例；重复 `--case` 可选择多个。测试语言为 Python，消费者为 C11，不需要 GoogleTest 或 C++ 编译器。`--config` 默认 Debug，可明确使用 Release；多配置生成器会读取对应配置的可执行文件。

本地 RED/GREEN 的命令与结果由实施记录引用；脚本不会自动提交、暂存或更新依赖。版本、ABI 与 target 类型检查不等同于 Git SHA 校验。
