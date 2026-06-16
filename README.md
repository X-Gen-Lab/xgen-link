# xgen-link Protocol Stack

Production-oriented MCU multi-node reliable protocol stack.

## Highlights

- Production v2 wire format: 24-byte base header, TLV extensions, 16-bit node IDs, and 32-bit packet numbers.
- Multi-node reliability: routed unicast, ACK ranges, SACK, adaptive retransmission, and connection-scoped peer state.
- Authenticated transport: production preset requires an auth provider; zero-copy send preserves authentication requirements.
- Embedded runtime: allocator control, no-heap smoke validation, and `xgl_next_deadline_ms()` for low-power scheduling.
- Release gates: unit/property/integration tests, SDK consumer smoke, static analysis, footprint, noheap smoke, and documentation build.

## Quick Build

```sh
cmake --preset gcc-test
cmake --build build/gcc-test --target xgl_tests
ctest --preset gcc-test --output-on-failure
```

## Release Validation

```sh
cmake --build build/gcc-test --target xgl_release_validation
```

## Documentation

Install documentation dependencies:

```sh
python -m pip install -r docs/requirements.txt
```

Build the bilingual MkDocs site:

```sh
mkdocs build --strict
```

Build documentation through CMake, including generated Doxygen public API:

```sh
cmake --preset ci
cmake --build build/ci --target xgl_docs
```

Documentation source:

- Chinese: [docs/zh/index.md](docs/zh/index.md)
- English: [docs/en/index.md](docs/en/index.md)
- Documentation build notes: [docs/README.md](docs/README.md)

## Requirements

- CMake 3.21+
- C11 compiler
- C++20 compiler for tests
- Python 3 with MkDocs dependencies for documentation
- Doxygen for generated C API reference
- cppcheck for release static analysis

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Copyright (c) 2026 X-Gen Lab
