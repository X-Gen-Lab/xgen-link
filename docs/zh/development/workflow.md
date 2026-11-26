# 开发工作流

使用 C11 生产构建和严格 C++17 主机测试。安装 `tools/quality.json` 固定提交的共享 `xgen-quality` 和该检查所需工具。配置及编译使用已准备的依赖。

## 行为与回归

新增或修复行为时，先运行能触发问题的生产路径测试，记录实际失败，再修复并复跑。优先替换外部 PHY、时钟或 allocator 以控制输入。迁移等价行为时保留历史用例、标签、性质测试种子与差分记录。

从仓库根目录，按[构建与消费](../guides/build-and-consume.md)配置并构建主机测试，再执行：

```sh
python tools/quality.py text
python tools/quality.py format
python tools/quality.py test --build-dir build/gcc-test
```

共享运行器检查 CTest 清单和实际 JUnit。失败时保留首次错误和可复现命令。性质测试支持 `XGL_PROPERTY_SEED` 固定随机输入。

## 分析与文档

使用实际编译数据库进行 Cppcheck 或 clang-tidy。显式准备工具，从根目录调用 `python tools/quality.py cppcheck --build-dir <dir>` 或 `python tools/quality.py tidy --build-dir <dir>`。诊断例外依[工程约定](../../standards.md)记载。文档检查使用[文档维护](documentation.md)中的独立入口。

## 完成条件

记录基线、工具链、profile、命令、退出结果及实际产物。按改动涉及的生命周期、wire 或资源边界选择[验证范围](validation.md)。先运行有针对性的检查，再运行交付所需的完整矩阵。
