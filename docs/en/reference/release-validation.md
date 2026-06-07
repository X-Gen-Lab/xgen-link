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
