---
id: development/documentation
type: how-to
audience: maintainer
source_language: zh_CN
---

# 文档维护

根据读者任务选择文档类型。教程提供一条可运行路径，指南说明条件、步骤和失败恢复，解释页阐明取舍，参考页维护可检查的合同。标明适用版本、档位和验证范围。

## 内容与翻译

中文规范正文位于 `docs/zh/`。每页用 `id`、`type`、`audience` 和 `source_language` 声明用途。英文译文位于 `docs/locale/en/LC_MESSAGES/<id>.po`。检查状态、API 名称、失败语义、数值和单位一致。字段与布局规则归 Wire v1 参考，声明与调用上下文归公开头 Doxygen 注释。

完整示例取自维护的可执行示例和测试。片段说明省略的初始化、配置和退出处理。MyST 构建只渲染内容。图示使用 `docs/sphinx/diagrams/` 下的双语本地 SVG。核对旧页面指路入口、两种语言和历史标题锚点。历史验证和失败证据关联各自的固定提交。

## 构建与验证

正式工具环境使用 Linux x86_64 和 CPython 3.12。先在仓库根目录显式准备哈希锁定工具，再使用已准备的前缀离线验证。

Windows 和 WSL 共用检出时，为两个系统分别准备虚拟环境和构建目录。Windows 可用 `.\.venv\Scripts\python.exe` 做离线源检查。WSL 使用自己的 `python3.12` 准备正式文档工具，示例前缀可换为 `build/docs-tools-wsl`，输出可换为 `build/documentation-wsl`。Windows 本地预览可用已准备的 Windows 原生 Sphinx、MyST、Breathe、polib 与 Doxygen，运行对应 Python 的 `docs build --mode preview --output build/documentation-windows`。正式 CI 使用锁定的 Linux 前缀。

```sh
python tools/documentation/install.py --prefix build/docs-tools \
  --cache build/docs-cache --allow-network
python tools/dev/dev.py docs check --report build/docs-check.json
python tools/dev/dev.py docs build --tool-prefix build/docs-tools \
  --mode verify --language all --output build/documentation
```

可用 `cmake -S docs -B build/docs -DXGL_DOCS_TOOL_PREFIX=build/docs-tools` 配置独立入口，再执行 `cmake --build build/docs --target xgl_docs`。在干净检出或源码归档上验证 `site/zh/`、`site/en/`、双语 Breathe C API 与 `/api/doxygen/html/` 兼容参考。`documentation-build.json` 记录实际源、工具、退出结果与执行目录。文档构建与 Pages 发布分别记录。

预览使用 `--mode preview` 和独立输出目录。在本次 `attempt-*/gettext/` 下查看提取的 POT，按实际 `msgid` 更新英文 PO，移除退役消息和 `fuzzy` 标记。语义审查后将当前中文文件的 SHA-256 写到 `X-XGL-Source-SHA256`，并标记 `X-XGL-Reviewed: true`。验证会核对完整消息。使用 `docs serve --build <输出目录>` 本地浏览。预览中的缺译会回退中文，并记录到报告。提交时 `docs check --staged` 读取暂存版本并执行离线检查。参阅[开发工作流](workflow.md)。
