# Static Analysis

Compiler baseline:

- `-Wall`
- `-Wextra`
- `-Werror`
- `-Wpedantic`
- conversion/sign/cast warnings

cppcheck baseline:

```sh
cmake --build build/gcc-test --target xgl_static_analysis
```

Release environments must install cppcheck. Unavailable static analysis is not a release pass.

Audit focus:

- v2 wire header offset encoding
- allocator failure paths
- fragment reassembly lifetime
- callback reentrancy
- ISR-to-protocol-task boundary
