# 文档规范

## 双语同步

中文和英文页面保持同名路径，例如：

- `docs/zh/protocol/wire-format.md`
- `docs/en/protocol/wire-format.md`

修改协议语义时必须同步两种语言。

## 术语

| English | 中文 |
| --- | --- |
| wire format | 线格式 |
| frame | 帧 |
| packet number | 包号 |
| ACK range | 确认范围 |
| reassembly | 重组 |
| auth trailer | 认证尾部 |

## 验证

```sh
mkdocs build --strict
cmake --build build/ci --target xgl_docs
```

## 写作规则

- 一页只定义一个主题，避免把 wire、security、routing 混在一起。
- 字段规范使用表格，必须包含 size、编码和失败规则。
- 能从代码验证的内容要使用真实常量和类型名。
- 未实现能力必须写 reserved，不写成生产可用。
- 示例代码必须能对应公共 API 头，而不是内部测试接口。

## 变更流程

1. 修改中文页。
2. 同步英文页。
3. 如涉及 API，检查 Doxygen 注释。
4. 如涉及示例，更新 `examples/*/README.md`。
5. 运行严格文档构建。
