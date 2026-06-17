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
powershell -ExecutionPolicy Bypass -File ./tools/docs_qa.ps1
# CI uses: pwsh ./tools/docs_qa.ps1
mkdocs build --strict
cmake --build build/ci --target xgl_docs
```

## Writing Rules

- One page owns one topic; do not mix wire, security, and routing definitions.
- Field specifications use tables with size, encoding, and failure rules.
- Use real constants and type names for facts that can be verified in code.
- Unimplemented capabilities must be documented as reserved, not production-ready.
- If a protocol detail cannot be confirmed from source or tests, write
  `TODO(xgen-link): confirm ...` instead of guessing.
- Example code must match public API headers, not internal test interfaces.
- Use [Glossary](../reference/glossary.md) terms for peer scope, AAD, route MTU,
  fragment budget, ACK range, and SACK.

## Canonical Snippets

- Public API snippets live in `reference/public-api.md` and Doxygen comments.
- End-to-end user workflows live in `getting-started/quick-start.md`.
- Example-specific behavior lives in `examples/*/README.md`.
- Protocol field layouts live only in `protocol/wire-format.md` and
  `protocol/extensions.md`.

Do not copy a long snippet into multiple pages. Link to the owner page or keep
the local snippet intentionally short.

## Change Flow

1. Update the Chinese page.
2. Sync the English page.
3. If a wire field, TLV, state machine, routing rule, security rule, or
   reliability semantic changed, update the matching protocol page and the
   traceability table in `protocol/implementation-map.md`.
4. If API changed, check Doxygen comments.
5. If examples changed, update `examples/*/README.md`.
6. Run docs QA and strict documentation build.
