# GitHub CI/CD

XGL 的 GitHub 自动化分成两条流水线：

- `CI`：验证代码、测试、静态分析、footprint、SDK consumer smoke、no-heap smoke 和文档构建。
- `Deploy Docs`：在 `main` 分支构建 MkDocs + Doxygen 文档站，并部署到 GitHub Pages。

## CI 触发条件

`CI` 在以下事件触发：

- 推送到 `main`。
- 面向 `main` 的 pull request。
- 手动 `workflow_dispatch`。

核心命令与本地 release gate 保持一致：

```sh
cmake --preset ci
cmake --build build/ci --target xgl_release_validation --parallel
```

这保证 GitHub 上的判断标准与本地发布验证一致。

## CI Jobs

| Job | 目的 | 主要检查 |
| --- | --- | --- |
| `release-validation` | 完整发布门禁 | CTest、cppcheck、SDK smoke、no-heap smoke、footprint、文档 |
| `gcc-smoke` | 快速 GCC 构建与测试 | `gcc-test` preset、示例、CTest |

`release-validation` 会上传 `build/ci/footprint/xgl-footprint.txt` 作为 artifact，便于比较 MCU 资源变化。

## 文档部署

`Deploy Docs` 只在 `main` 分支推送或手动触发时运行。部署使用 GitHub Pages 官方 artifact 流程：

```sh
cmake --preset ci
cmake --build build/ci --target xgl_docs --parallel
```

上传目录为：

```text
build/ci/docs/site
```

该目录包含 MkDocs 双语站点和 Doxygen 公共 API reference。

## GitHub Pages 设置

仓库需要在 GitHub 中启用：

1. 打开 repository `Settings`。
2. 进入 `Pages`。
3. 将 source 设置为 `GitHub Actions`。
4. 保存后再次运行 `Deploy Docs`。

部署成功后，workflow 的 `github-pages` environment 会显示最终 URL。

## 失败排查

| 失败点 | 常见原因 | 处理方式 |
| --- | --- | --- |
| Configure 失败 | CMake preset 或系统依赖不匹配 | 查看 `cmake --preset ci` 日志 |
| Static analysis 失败 | cppcheck 发现 warning/style/performance/portability 问题 | 修复源码或增加合理 inline suppression |
| Docs 失败 | MkDocs 严格模式断链、Doxygen 配置错误 | 本地运行 `cmake --build build/ci --target xgl_docs` |
| Tests 失败 | 协议行为回归、示例 API 漂移 | 查看 CTest failure output |
| Pages 失败 | Pages source 未设为 GitHub Actions，或权限不足 | 检查 repository Pages 设置和 workflow permissions |

## 发布原则

- PR 必须通过 `CI` 后再合并。
- `main` 分支文档部署失败时，不应发布 release。
- 不把生成的 `site/` 或 Doxygen HTML 提交到仓库。
- release candidate 应同时保留 CI run、footprint artifact 和 release validation 记录。
