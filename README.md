# xgen-link Protocol Stack

Modern, robust, and highly configurable communication protocol stack for resource-constrained embedded systems.

## Features

- **Multi-instance architecture**: Support multiple independent protocol stacks
- **MVP communication loop**: Single-link point-to-point send/receive, optional ACK-based reliable send, timeout retransmission, and RX callback
- **Compile-time configuration**: Eliminate unused code through Kconfig
- **Thread safety**: Optional mutex protection for RTOS environments
- **Bare-metal support**: Run without RTOS dependencies
- **Minimal footprint**: 32KB RAM, 50KB Flash for basic configuration
- **Industrial quality**: Comprehensive testing, documentation, and CI/CD

## MVP Scope

The current acceptance baseline focuses on a single physical link with point-to-point communication. Unreliable send, reliable send, ACK handling, timeout retransmission, and receive callbacks are implemented and covered by tests.

Compression, encryption, and true zero-copy transmission are not part of the current MVP. Related constants and API shapes may exist for compatibility, but they should be treated as placeholders until implementation and acceptance tests are added.

## Quick Start

### Build

```bash
# Configure with CMake presets
cmake --preset debug

# Build
cmake --build build/debug

# Run tests
ctest --preset test
```

### Example Usage

```c
#include <xgl/xgl.h>

int main(void) {
    /* Get default configuration */
    xgl_config_t config;
    xgl_config_get_default(&config);
    
    /* Create protocol instance */
    xgl_handle_t handle = xgl_create(&config);
    
    /* Initialize */
    xgl_init(handle);
    
    /* Use the protocol... */
    
    /* Cleanup */
    xgl_destroy(handle);
    
    return 0;
}
```

## Documentation

- [Architecture Overview](docs/architecture.md)
- [API Reference](docs/api.md)
- [Porting Guide](docs/porting.md)
- [Examples](examples/)

## Requirements

- CMake 3.21+
- C11 compiler (GCC, Clang, or MSVC)
- C++17 compiler (for tests)
- Google Test 1.14.0 (automatically fetched)

## License

Copyright (c) 2026 Nexus Team

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
