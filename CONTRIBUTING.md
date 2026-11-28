# 为 XGen Link 贡献改动

从改动涉及的接口开始。修改收发行为先查公开 API 和 Wire v1，调整构建选项先查档位及导出身份，调整工作区先查资源模型。提交说明要能让维护者复现原问题、识别受影响的产品并复跑验证。[公开 API](docs/zh/reference/api.md) · [Wire v1](docs/zh/reference/protocol/v1/index.md) · [编译档位](docs/zh/reference/profiles.md) · [资源模型](docs/zh/explanation/resource-model.md)

## 准备维护环境

在仓库根目录使用 Python 3.12、Git，以及能配套编译 C11 和 C++17 的主机工具链。Windows 使用 MSVC 时从已初始化编译器的开发者终端运行。首次执行下列命令会准备仓库工具、安装 Git 钩子并下载锁定的 GoogleTest。配置和构建复用已经准备好的依赖。

```sh
python scripts/setup/install_dev_tools.py
python tools/dev/dev.py doctor
python tools/dev/dev.py prepare --scope host --allow-network
python tools/dev/dev.py check --preset native-debug
```

使用自备 GoogleTest 1.16.0 时，在最后一条命令追加 `--gtest-dir` 并传入含 `GTestConfig.cmake` 的目录。准备工作流变更时，另运行 `python tools/dev/dev.py prepare --scope workflow --allow-network`。生产 SDK 的独立构建步骤见[项目首页](README.md)。

## 选择验证范围

| 改动范围 | 需要核对的结果 |
| --- | --- |
| C 实现或公开头 | 通过受影响的生产入口复现并修复问题。运行主机 CTest，核对受影响的档位、错误返回、借用期限，以及公开头在 C11、C++17 中的可用性。 |
| Wire、收发或转发 | 运行对应帧向量、边界用例和独立端点双向互通。涉及兼容性时记录两个端点的源码版本和档位。 |
| 档位、资源、CMake 或 SDK 导出 | 在独立前缀安装和搬移 SDK，编译、链接并运行外部消费者。比较生成头、导出能力与资源身份。 |
| Boot 工作区或热路径 | 用相同工具链和资源组合比较 ELF、map、静态 RAM、工作区和栈。涉及产品驱动时补充对应板级观测。 |
| 文档、维护脚本或 CI | 文档改动同步中文源和英文 PO，严格构建双语站点。工具改动运行对应合同测试，工作流改动运行 actionlint。 |

开发入口将命令日志写入 `build/<preset>/developer-commands/`。严格 CTest 在构建目录生成 `ctest-inventory.json` 和 `ctest.xml`。性质测试可用 `XGL_PROPERTY_SEED` 回放。具体的主机矩阵、独立 SDK、ARM 和文档命令见[开发工作流](docs/zh/development/workflow.md)、[验收说明](docs/zh/development/validation.md)和[文档维护](docs/zh/development/documentation.md)。

## 提交与评审

1. 缺陷报告写明提交或 SDK 版本、档位、编译器、资源选项、最小复现步骤、期望结果和实际结果。保留首次失败的命令与原始输出。
2. 控制改动范围，保留公开标识和受支持的调用方式。新增或修复行为时，用受影响入口取得失败与通过的结果。纯迁移则比较前后清单、配置、接口和实际行为。[工程约定](docs/standards.md)
3. 暂存本次改动后提交。提交钩子读取暂存区，按变更路径执行格式、离线文档、工作流和行为检查。标题采用 Conventional Commits，例如 `fix(transport): 修复重试后 ACK 状态丢失`。涉及不兼容改动时，在说明中写清升级步骤。
4. 在合并请求中列出变更动机、影响的档位、实际运行的命令和结果。将用户可见的变化按版本记入[变更记录](CHANGELOG.md)，同时更新公开合同和配套译文。
