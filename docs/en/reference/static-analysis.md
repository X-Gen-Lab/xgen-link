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

## Expected Failure Handling

| Failure | Action |
| --- | --- |
| Compiler warning promoted by `-Werror` | Fix code or make the conversion/cast explicit |
| cppcheck finding in production code | Fix code, add a targeted suppression only when the finding is demonstrably false positive |
| cppcheck unavailable | Treat as environment failure for release validation |
| Generated/build output warning | Prefer excluding generated output from analysis instead of suppressing source files |

Suppressions must be narrow: prefer line-level or symbol-level suppressions with
a short reason near the code. Do not add broad file suppressions for protocol
paths that touch wire format, allocator lifetimes, authentication, or fragment
reassembly.

Audit focus:

- v2 wire header offset encoding
- allocator failure paths
- fragment reassembly lifetime
- callback reentrancy
- ISR-to-protocol-task boundary
