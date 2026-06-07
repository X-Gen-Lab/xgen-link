# Build and Test

## GCC Test Build

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_tests
ctest --preset gcc-test --output-on-failure
```

## Release Gate

```sh
cmake --build build/gcc-test --target xgl_release_validation
```

This target aggregates unit tests, SDK consumer smoke, noheap smoke, footprint generation, and cppcheck static analysis.

## Documentation Build

```sh
mkdocs build --strict
cmake --preset ci
cmake --build build/ci --target xgl_docs
```

`xgl_docs` generates the public Doxygen API reference and then builds the MkDocs bilingual site.

## Troubleshooting

- GCC presets require `gcc` and `g++` in PATH.
- Documentation failures are usually missing dependencies, broken links, or missing Doxygen.
- Release environments must install `cppcheck`; unavailable static analysis is not an acceptance condition.
