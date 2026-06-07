# Documentation Rules

## Bilingual Sync

Chinese and English pages keep matching paths, for example:

- `docs/zh/protocol/wire-format.md`
- `docs/en/protocol/wire-format.md`

Protocol semantic changes must update both languages.

## Terminology

| English | Chinese |
| --- | --- |
| wire format | 线格式 |
| frame | 帧 |
| packet number | 包号 |
| ACK range | 确认范围 |
| reassembly | 重组 |
| auth trailer | 认证尾部 |

## Verification

```sh
mkdocs build --strict
cmake --build build/ci --target xgl_docs
```

## Writing Rules

- One page owns one topic; do not mix wire, security, and routing definitions.
- Field specifications use tables with size, encoding, and failure rules.
- Use real constants and type names for facts that can be verified in code.
- Unimplemented capabilities must be documented as reserved, not production-ready.
- Example code must match public API headers, not internal test interfaces.

## Change Flow

1. Update the Chinese page.
2. Sync the English page.
3. If API changed, check Doxygen comments.
4. If examples changed, update `examples/*/README.md`.
5. Run strict documentation build.
