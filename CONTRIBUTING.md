# Contributing to xgen-link

Thank you for your interest in contributing to xgen-link! This document provides guidelines for contributing to the project.

## Code of Conduct

Please be respectful and constructive in all interactions.

## Development Environment Setup

### Prerequisites

```bash
# Windows
winget install Kitware.CMake
winget install Git.Git
# Install Visual Studio 2019+ or Build Tools

# Linux (Ubuntu/Debian)
sudo apt-get install cmake gcc g++ git

# macOS
brew install cmake git
```

### Clone and Build

```bash
git clone <repository-url>
cd xgen-link

# Configure with CMake preset
cmake --preset debug

# Build
cmake --build build/debug

# Run tests
ctest --preset test
```

### IDE Setup

**VS Code** (recommended):
- Open the `xgen-link` folder
- Install recommended extensions (C/C++, CMake Tools)
- Use CMake presets for configuration

## How to Contribute

### Reporting Bugs

1. Check existing issues to avoid duplicates
2. Use the bug report template (if available)
3. Include:
   - Platform and version (Windows/Linux/macOS, compiler version)
   - Steps to reproduce
   - Expected vs actual behavior
   - Relevant logs or error messages

### Suggesting Features

1. Check existing feature requests
2. Describe the use case and benefits
3. Consider impact on resource-constrained systems
4. Discuss API design implications

### Pull Requests

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Ensure tests pass: `ctest --preset test`
5. Follow code style guidelines
6. Commit with conventional commits: `feat(transport): add congestion control`
7. Push and create a Pull Request

## Code Style

### C Code

- Follow `.clang-format` configuration
- Use Doxygen comments with **backslash style** (`\brief`, `\param`)
- Maximum line length: 80 characters
- Indent: 4 spaces (no tabs)
- Pointer alignment: left (`char* ptr`, not `char *ptr`)

### Doxygen Comment Style

Use **backslash style** for all Doxygen comments:

```c
/**
 * \file            xgl_transport.h
 * \brief           Transport layer interface
 * \author          Nexus Team
 * \version         2.0.0
 * \date            2026-01-XX
 *
 * \copyright       Copyright (c) 2026 Nexus Team
 */

/**
 * \brief           Initialize transport layer
 * \param[in]       handle: Protocol instance handle
 * \param[in]       config: Pointer to configuration structure
 * \return          XGL_OK on success, error code otherwise
 * \note            Must be called before using transport layer
 */
xgl_status_t xgl_transport_init(xgl_handle_t handle,
                                const xgl_transport_config_t* config);
```

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Files | snake_case | `xgl_transport.c` |
| Functions | snake_case with xgl_ prefix | `xgl_transport_init()` |
| Types | snake_case_t | `xgl_config_t` |
| Macros | UPPER_CASE with XGL_ prefix | `XGL_MAX_PACKET_SIZE` |
| Enums | UPPER_CASE with XGL_ prefix | `XGL_STATUS_OK` |

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`

Scopes: `core`, `datalink`, `network`, `transport`, `platform`, `test`, `build`, `docs`

Examples:
```
feat(transport): add congestion control algorithm
fix(datalink): correct frame checksum calculation
docs(api): update transport layer documentation
test(network): add routing table tests
```

## Testing

### Testing Requirements

**All contributions must include appropriate tests.** Testing is critical for maintaining protocol reliability.

#### When Adding New Features

1. **Unit Tests Required**: Write unit tests for new functionality
2. **Integration Tests**: Add integration tests for cross-layer features
3. **Property Tests**: Consider property-based tests for protocol correctness
4. **Test Coverage**: Maintain or improve overall coverage

#### When Modifying Existing Code

1. **Run All Tests**: Ensure all existing tests still pass
2. **Update Tests**: Modify tests if behavior changes are intentional
3. **Add Tests**: Add new tests for newly covered scenarios
4. **No Regression**: Do not reduce test coverage

### Running Tests

#### Quick Start

```bash
# Configure with tests enabled
cmake --preset debug

# Build
cmake --build build/debug

# Run all tests
ctest --preset test

# Or run test executable directly
./build/debug/test/xgl_tests
```

#### Running Specific Tests

```bash
# Run specific test suite
./build/debug/test/xgl_tests --gtest_filter="Transport*"

# Run specific test case
./build/debug/test/xgl_tests --gtest_filter="TransportTest.BasicSend"

# Run with verbose output
./build/debug/test/xgl_tests --gtest_verbose
```

### Writing Tests

#### Test File Organization

Tests use Google Test framework. Each module should have corresponding test files:

```
test/
├── test_<module>.cpp       # Unit tests
├── integration/            # Integration tests
├── property/              # Property-based tests
└── mocks/                 # Mock implementations
```

#### Unit Test Example

```cpp
/**
 * \file            test_transport.cpp
 * \brief           Transport layer unit tests
 */

#include <gtest/gtest.h>

extern "C" {
#include "xgl/xgl.h"
#include "xgl/internal/xgl_transport.h"
}

class TransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        xgl_config_get_default(&config);
        handle = xgl_create(&config);
        ASSERT_NE(nullptr, handle);
    }
    
    void TearDown() override {
        if (handle != nullptr) {
            xgl_destroy(handle);
        }
    }
    
    xgl_config_t config;
    xgl_handle_t handle = nullptr;
};

TEST_F(TransportTest, BasicInitialization) {
    ASSERT_EQ(XGL_OK, xgl_init(handle));
}

TEST_F(TransportTest, SendPacket) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    ASSERT_EQ(XGL_OK, xgl_send(handle, data, sizeof(data)));
}
```

### Test Best Practices

1. **Independence**: Each test should run independently
2. **Repeatability**: Tests should produce consistent results
3. **Clear Assertions**: Use descriptive assertion messages
4. **Single Concept**: Each test should verify one concept
5. **Cleanup**: Always clean up resources in TearDown()
6. **Documentation**: Comment what each test validates

### Pre-Submission Checklist

Before submitting a PR, verify:

- [ ] All new code has corresponding tests
- [ ] All tests pass locally: `ctest --preset test`
- [ ] Code follows style guidelines (run clang-format)
- [ ] No compiler warnings
- [ ] Documentation is updated
- [ ] Commit messages follow conventional commits

## Documentation

### API Documentation

- Use Doxygen comments for all public APIs
- Include parameter descriptions
- Document return values and error conditions
- Provide usage examples where appropriate

### Building Documentation

```bash
# Generate API documentation (if Doxygen is configured)
doxygen Doxyfile
```

### Documentation Guidelines

- Update API documentation for public interfaces
- Add examples for new features
- Keep README up to date
- Document configuration options in Kconfig

## Performance Considerations

When contributing to xgen-link, keep in mind:

1. **Memory Efficiency**: Minimize RAM and Flash usage
2. **CPU Overhead**: Optimize critical paths
3. **Zero-Copy**: Avoid unnecessary data copying
4. **Compile-Time Configuration**: Use Kconfig to eliminate unused code
5. **Resource Constraints**: Test on target hardware when possible

## Platform Abstraction

When adding platform-specific code:

1. Use the platform abstraction layer (xgl_platform.h)
2. Implement platform-specific functions in src/platform/
3. Ensure bare-metal compatibility
4. Test on multiple platforms if possible

## Review Process

1. Automated CI checks must pass (if configured)
2. At least one maintainer approval required
3. Address all review comments
4. Squash commits if requested

## Questions?

Open an issue or discussion for questions about:
- Protocol design decisions
- Implementation approaches
- Testing strategies
- Platform porting

Thank you for contributing! 🎉
