# 文档维护

选择读者要完成的任务，以[站点入口](../index.md)分类。教程提供一条可运行路径；指南说明条件、步骤和失败恢复；解释页阐明取舍；参考页维护可检查的合同。标明适用版本、档位和验证范围。

## 内容与翻译

先修改中文规范正文，再核对英文同路径页面。检查状态、API 名称、失败语义、数值和单位一致。字段与布局规则归 wire v3 参考；声明与调用上下文归公共头 Doxygen 注释。每项规则只有一处权威正文。导航、页间链接和图示引用该处。

完整示例取自仓库维护的可执行示例和测试。片段说明所省略的初始化、配置及退出处理。构建只渲染内容，不执行 Markdown 中的命令。保留已发布旧页面的指路入口，并核对两类 URL。把历史验证和失败证据固定到对应提交。

## 构建与验证

准备 CMake 3.24 或以上、Doxygen、PowerShell 7 和 Python。文档构建使用当前仓库、公共头与已准备好的工具。在仓库根目录执行：

```sh
python -m pip install -r docs/requirements.txt
pwsh -File tools/docs_qa.ps1
cmake -S docs -B build/docs
cmake --build build/docs --target xgl_docs
```

严格构建成功后查看 `build/docs/site/index.html`、中英文入口和生成 API。将 C API 导航页 `docs/api/index.md` 纳入 Git 跟踪，并在干净检出中验证文档变更。工作流失败时，查看其 `documentation-diagnostics` 产物，其中保存了失败前收集的日志和工具版本。当前分支的构建成功与 Pages 发布分别记录。

可用 `mkdocs serve` 预览 Markdown，使用完整构建查看生成 C API。Pre-commit 与文档检查命令参见[开发工作流](workflow.md)。
