# Release Validation

Validate release candidates from a clean build directory.

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_release_validation
ctest --preset gcc-test --output-on-failure
```

## Gates

- Unit, property, and integration tests pass.
- SDK consumer smoke passes.
- noheap smoke passes.
- footprint report is generated.
- cppcheck is installed and passes.
- docs build passes when `XGL_BUILD_DOCS=ON`.
- worktree contains only explicitly accepted user changes.

## Documentation Gate

```sh
mkdocs build --strict
cmake --build build/ci --target xgl_docs
```

`xgl_docs` validates both the MkDocs bilingual site and the Doxygen public API reference. Broken links, missing pages, and Doxygen configuration errors block release.

## Recommended Order

1. Configure from a clean build directory.
2. Build `xgl_tests`.
3. Run CTest.
4. Build `xgl_release_validation`.
5. Build `xgl_docs`.
6. Check `git status --short`.

## Wire Format Compatibility Commitments

| Commitment | Description |
| --- | --- |
| Base header layout | 24-byte fixed layout unchanged, compatible across all versions |
| Byte order | Always little-endian, never changes |
| Magic value | `A5 5A` unchanged |
| Version field | Currently fixed `2`, incremented on major version upgrades |
| TLV extensions | Backward compatible: new extensions do not affect old receivers (unknown TLVs are ignored) |
| Flags reserved bits | Unused bits (`0xC0`) reserved, not used for new features |
| Packet Type reserved values | 4–7 reserved; old versions should ignore them if used in the future |

## API Stability Commitments

| Layer | Stability | Description |
| --- | --- | --- |
| `xgl.h` public API | Stable | Semantically compatible; no removal or renaming of existing functions |
| `xgl_types.h` types | Stable | No removal of existing fields; new fields may be appended |
| `xgl_config.h` configuration | Stable | No removal of existing config items; new items may be appended |
| `xgl_error.h` error codes | Stable | No renumbering of existing error codes |
| `include/xgl/internal/` | Internal | Not a stable ABI; may be freely modified |

## Version Upgrade Strategy

- **Minor version**: New features, new extensions, new Kconfig options; no breaking changes to existing API or wire format.
- **Major version**: May modify wire format base header, remove deprecated APIs; migration path must be documented.