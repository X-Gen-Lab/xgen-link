# 测试策略

## 归属

各独立基础仓库负责自身行为：xgen-memory 的分配器/池、xgen-containers 的容器、xgen-crc 的校验、xgen-bytes 的字节读写，以及 xgen-status 的通用状态。协议测试覆盖 wire 编码、分层接口、路由、peer 状态、分片、安全及公开实例行为。删除公共组件包装时同时删除对应的重复协议测试；协议对公共服务的真实集成仍需验证。

## 回归场景

用真实协议状态转换验证 ACK 原子性、保留 RX 所有权、背压、重传及显式 scope 关闭。容量测试检查精确工作区测量、不足或未对齐拒绝、一次后端预留、运行回收及销毁。静态认证双端验证重传使用新安全序号、应用只交付一次。

## 执行

通过 CTest 运行协议测试、主机示例、静态生命周期及安装后的 C 消费者。Boot 和 Embedded 分开配置，参见[验证矩阵](../reference/validation-matrix.md)。随机性质测试补充固定回归，不能代替 sanitizer、fuzz 或硬件验收。

安装依赖后可使用根工程 presets；独立源码开发使用 `cmake -S dev` 和五个 `XGL_DEV_*_SOURCE_DIR`，具体见[构建与测试](../getting-started/build-and-test.md)。dev 的 CTest 根目录为对应 `build/dev-<profile>`；正式模块作为产品子目录时默认不添加示例、smoke 或发布辅助。新入口和旧阶段结果分别记录。

## 风格

遵循仓库 clang-format 和 Doxygen 格式。新增测试应证明不变量或缺陷回归，不复制实现。测试必须保持配置/PHY 的借用生命周期并显式传入时间。

空行规则采用工程规范 C-020、C-021 和 DOC-013：独立定义和公共 API 文档组之间空一行，Doxygen 与对应声明紧邻；函数内按语义分段。clang-format 19.1.5 负责定义间隔和多余空行，共享 xgen-quality 的 `format` 入口补充常见公共 C 头的结构检查；不推断函数体的业务阶段。

显式安装 `tools/quality.json` 固定提交对应的 xgen-quality 0.1.0 包后执行：

```sh
python tools/quality.py format
python tools/quality.py text
python tools/quality.py test --build-dir build/dev-full
python tools/quality.py cppcheck --build-dir build/dev-full
python tools/quality.py tidy --build-dir build/dev-full
python tools/quality.py docs
python -m pre_commit run --all-files
```

检查只读，不会下载或改写源码。主动整理使用 clang-format 19.1.5 的 `-i`；公共声明分组按诊断和规范人工调整。编译数据库必须来自实际工具链和显式开发依赖。CI 默认使用官方仓库，允许通过仓库变量覆盖，所有源码固定完整提交。实际本地与远端结果分别记录在根目录 `REFACTORING_STATUS.md`。

## TDD 与测试清单

行为变更先增加能重现需求或缺陷的失败测试，记录 RED，再实现最小修复并验证 GREEN，最后整理实现并重跑相关回归。既有行为的覆盖补充、纯文档和排版不人为制造失败。

主机测试使用 GoogleTest/GoogleMock 1.16.0 和严格 C++17。`gtest_discover_tests` 逐个登记用例；分类脚本为其添加 `xgl`、`unit` 或 `integration` 标签，性质测试另加 `property`。示例、静态生命周期和安装消费也属于 `integration`。`test/cmake/baseline_tests.txt` 保存迁移前的 510 个用例名称，发现阶段逐名检查，不能用新增数量掩盖历史用例丢失。共享测试入口拒绝空集合、禁用、跳过、重复或失败的测试。

## 性质测试回放

种子优先级为命令行 `--xgl_property_seed=N`、环境变量 `XGL_PROPERTY_SEED`、固定默认值 `5785420`。无效或超出无符号整数范围的输入必须报错。每个测试使用稳定的独立随机流，过滤执行不改变其随机输入。JUnit 记录基础种子与测试流，失败时输出可复制的回放命令：

```sh
./build/dev-full/link/test/xgl_tests --gtest_filter='XglFrameProperties.*' --xgl_property_seed=5785420
ctest --test-dir build/dev-full -L property --output-on-failure
```

Windows 加 `.exe`，多配置生成器使用实际配置子目录。

## 覆盖率与验收

在独立的原生 GNU 构建目录启用 `-DXGL_ENABLE_COVERAGE=ON`，仅对生产 `xgl` target 插桩；依赖、GoogleTest 和测试代码不计入协议覆盖率。更换源码或编译配置后清理旧计数或新建构建目录，再执行全部测试：

```sh
python tools/quality.py test --build-dir build/coverage
python tools/quality.py coverage --build-dir build/coverage --gcov-executable gcov
```

行、函数和分支覆盖率分别至少 80%，不能用平均值替代其中一项，也不能通过排除难测的生产文件过关。非原生 GNU 编译器请求该覆盖率选项时明确拒绝。报告归档于 `out/reports`；主机、安装消费、sanitizer、ARM ELF 和硬件结论分别记录。
