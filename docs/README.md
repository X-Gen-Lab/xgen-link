# xgen-link Documentation Source

This directory contains the MkDocs source for the XGL bilingual documentation site.

- Chinese entry: [zh/index.md](zh/index.md)
- English entry: [en/index.md](en/index.md)
- Public C API reference is generated from Doxygen during the `xgl_docs` build.

Build locally:

```sh
python -m pip install -r docs/requirements.txt
mkdocs build --strict
```

Build through CMake:

```sh
cmake --preset ci
cmake --build build/ci --target xgl_docs
```
