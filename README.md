# xgen-link Protocol Stack

Modern, robust, and highly configurable communication protocol stack for resource-constrained embedded systems.

## Features

- **Multi-instance architecture**: Support multiple independent protocol stacks
- **Production v2 wire format**: 24-byte base header, TLV extensions, 16-bit node IDs, and 32-bit packet numbers
- **Multi-node reliability**: Routed unicast, ACK ranges, SACK, adaptive retransmission, and connection-scoped peer state
- **Authenticated transport**: Production preset requires an auth provider; zero-copy send preserves authentication requirements
- **Low-power runtime API**: `xgl_next_deadline_ms()` lets bare-metal and RTOS applications sleep until the next protocol deadline
- **Compile-time configuration**: Eliminate unused code through Kconfig
- **Thread safety**: Optional mutex protection for RTOS environments
- **Bare-metal support**: Run without RTOS dependencies
- **Minimal footprint**: 32KB RAM, 50KB Flash for basic configuration
- **Industrial quality**: Comprehensive testing, documentation, and CI/CD

## Production Scope

The current acceptance baseline targets a production-grade embedded multi-node
protocol stack: v2 wire encoding, route-aware forwarding, reliable and
unreliable delivery, ACK range/SACK handling, authenticated frame paths,
fragment reassembly budgets, and receive callbacks are implemented and covered
by unit, property, integration, SDK consumer, and footprint tests.

Compression and encryption are reserved codec capabilities and are rejected
until the payload expansion and security model are wired into the production
path. `xgl_send_zerocopy` supports single-frame sends while preserving the
authentication model; reliable zero-copy requests may copy into the reliable
queue to keep retransmission semantics.

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

### Windows Toolchains

The default presets use the compiler selected by CMake. If Clang selects the
MSVC runtime, run CMake from a Visual Studio Developer PowerShell so
`msvcrtd.lib` and `oldnames.lib` are available.

For a MinGW/GCC toolchain on Windows, use the GCC presets:

```bash
cmake --preset gcc-test
cmake --build --preset gcc-test
ctest --preset gcc-test
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
    
    /* Use the protocol. In low-power loops, sleep until
       xgl_next_deadline_ms(handle) or PHY RX activity wakes the task. */
    
    /* Cleanup */
    xgl_destroy(handle);
    
    return 0;
}
```

## Documentation

- [Architecture Overview](docs/architecture.md)
- [API Reference](docs/api.md)
- [Porting Guide](docs/porting.md)
- [Production SDK Plan](docs/production_sdk_plan.md)
- [Resource Model](docs/resource_model.md)
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
