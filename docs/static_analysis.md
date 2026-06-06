# Static Analysis Baseline

The embedded C baseline is:

- Build with `-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wcast-align -Wcast-qual`.
- Keep third-party dependencies outside project-level `-Werror`.
- Run `cppcheck --enable=warning,style,performance,portability --std=c11` when available.
- Treat MISRA/CERT C findings as release blockers when they affect memory ownership, integer conversion, alignment, concurrency, or unchecked return values.

Known risk areas to audit before production certification:

- Packed frame header encoding/decoding.
- Fragment reassembly memory lifetime.
- Custom allocator failure paths.
- Callback reentrancy when thread safety is enabled.
- PHY drivers that call into xgen-link from interrupt context.
