---
id: development/workflow
type: how-to
audience: maintainer
source_language: zh_CN
---

# 开发工作流

使用 C11 生产构建和严格 C++17 主机测试。配置前准备 CMake、所选编译器和测试依赖。启用共享的 `pre-commit` 和 `commit-msg`，提交信息采用 Conventional Commits。

## 行为与回归

新增或修复行为时，先运行能触发问题的生产路径测试，记录实际失败，再修复并复跑。优先替换外部 PHY、时钟或 allocator 以控制输入。迁移等价行为时保留历史用例、标签、性质测试种子与差分记录。

从仓库根目录按[构建与消费](../guides/build-and-consume.md)准备 GoogleTest，再依次配置、构建并执行严格 CTest。

```sh
python tools/dev/dev.py configure --preset native-debug --gtest-dir /path/to/lib/cmake/GTest
python tools/dev/dev.py build --preset native-debug
python tools/dev/dev.py test --preset native-debug
```

CI 主机任务用 `python test/cmake/run_ctest.py --build-dir build/native-debug --config Debug` 保存并核对测试清单与 JUnit 结果。失败时保留首次错误和可复现命令。性质测试支持 `XGL_PROPERTY_SEED` 固定随机输入。

CI 的 `status` 任务汇总主机、裁剪档位、SDK、Wire、静态分析与 ARM 软件验收。文档工作流独立构建经验证的 Pages 部署产物。

## 文档

提交前离线检查暂存文档，再按[文档维护](documentation.md)执行 Sphinx/MyST 双语严格构建并验证生成的公开 API 参考。

## 完成条件

记录基线、工具链、profile、命令、退出结果及实际产物。按改动涉及的生命周期、wire 或资源边界选择[验证范围](validation.md)。先运行有针对性的检查，再运行交付所需的完整矩阵。
