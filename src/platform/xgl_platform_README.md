# Platform Detection (xgl_platform.h)

The platform detection module provides compile-time and runtime detection of platform characteristics for the xgen-link protocol stack.

## Features

### Compiler Detection

Automatically detects the compiler being used:

- **GCC** - GNU Compiler Collection
- **Clang** - LLVM Clang compiler
- **MSVC** - Microsoft Visual C++
- **ARM Compiler** - ARM Compiler toolchain
- **IAR** - IAR Embedded Workbench
- **Keil** - Keil MDK-ARM

Each compiler detection includes version information.

### Operating System Detection

Detects the target operating system:

- **Windows** - Windows (32-bit or 64-bit)
- **Linux** - Linux distributions
- **macOS** - Apple macOS
- **Unix** - Generic Unix-like systems
- **FreeRTOS** - FreeRTOS real-time operating system
- **Zephyr** - Zephyr RTOS
- **RT-Thread** - RT-Thread RTOS
- **Bare-Metal** - No operating system

### Architecture Detection

Detects the target CPU architecture:

- **ARM** - ARM processors (32-bit and 64-bit)
  - Cortex-M0/M0+
  - Cortex-M3
  - Cortex-M4
  - Cortex-M23
  - Cortex-M33
- **x86** - Intel/AMD 32-bit
- **x86-64** - Intel/AMD 64-bit
- **RISC-V** - RISC-V (32-bit and 64-bit)
- **MIPS** - MIPS (32-bit and 64-bit)
- **PowerPC** - PowerPC (32-bit and 64-bit)

### Endianness Detection

Detects byte order at compile-time and runtime:

- **Little-Endian** - LSB first (x86, ARM, most modern systems)
- **Big-Endian** - MSB first (some MIPS, PowerPC, network byte order)

Runtime verification available via `xgl_is_little_endian()`.

### Alignment Requirements

Detects memory alignment requirements:

- **Strict Alignment** - Required for ARM Cortex-M0/M0+, MIPS
  - 4-byte alignment required for multi-byte access
  - Unaligned access causes hardware fault
- **Relaxed Alignment** - x86, ARM Cortex-M3/M4/M7
  - Unaligned access supported (may be slower)
  - 1-byte alignment sufficient

## Usage

### Basic Platform Information

```c
#include <xgl/xgl_platform.h>

/* Get platform information */
xgl_platform_info_t info;
xgl_platform_get_info(&info);

printf("Compiler: %s (version %u)\n", info.compiler_name, info.compiler_version);
printf("OS: %s\n", info.os_name);
printf("Architecture: %s %s\n", info.arch_name, info.arch_subname);
printf("Pointer Size: %u-bit\n", info.pointer_size * 8);
printf("Endianness: %s\n", info.endian_name);
printf("Alignment: %s (%u-byte required)\n", 
       info.alignment_name, info.alignment_required);
```

### Platform Information String

```c
/* Generate formatted platform information */
char buffer[512];
int written = xgl_platform_info_string(buffer, sizeof(buffer));

printf("%s", buffer);
```

Output example:
```
Compiler: MSVC (version 1941)
OS: Windows
Architecture: x86-64
Pointer Size: 64-bit
Endianness: Little-Endian
Alignment: Relaxed (1-byte required)
```

### Compile-Time Detection

Use preprocessor macros for conditional compilation:

```c
/* Compiler-specific code */
#if defined(XGL_COMPILER_GCC)
    /* GCC-specific optimizations */
#elif defined(XGL_COMPILER_MSVC)
    /* MSVC-specific code */
#endif

/* OS-specific code */
#if defined(XGL_OS_WINDOWS)
    /* Windows-specific code */
#elif defined(XGL_OS_LINUX)
    /* Linux-specific code */
#elif defined(XGL_OS_BAREMETAL)
    /* Bare-metal code */
#endif

/* Architecture-specific code */
#if defined(XGL_ARCH_ARM_CORTEX_M0)
    /* Cortex-M0 specific code */
#elif defined(XGL_ARCH_X86_64)
    /* x86-64 specific code */
#endif

/* Endianness-specific code */
#if defined(XGL_LITTLE_ENDIAN)
    /* Little-endian byte order */
#elif defined(XGL_BIG_ENDIAN)
    /* Big-endian byte order */
#endif

/* Alignment-specific code */
#if defined(XGL_STRICT_ALIGNMENT)
    /* Use byte-wise access for multi-byte fields */
#else
    /* Direct pointer access is safe */
#endif
```

### Runtime Endianness Check

```c
/* Verify endianness at runtime */
if (xgl_is_little_endian()) {
    printf("System is little-endian\n");
} else {
    printf("System is big-endian\n");
}
```

### Alignment Utilities

```c
/* Check if pointer is aligned */
void* ptr = malloc(100);
if (xgl_is_aligned(ptr, 4)) {
    printf("Pointer is 4-byte aligned\n");
}

/* Align pointer up */
void* aligned_ptr = xgl_align_up(ptr, 8);
printf("Aligned to 8 bytes: %p -> %p\n", ptr, aligned_ptr);

/* Align size up */
size_t size = 13;
size_t aligned_size = xgl_align_size(size, 4);
printf("Aligned size: %zu -> %zu\n", size, aligned_size);  /* 13 -> 16 */
```

### Compiler Attributes

Use portable compiler attributes:

```c
/* Force inline */
XGL_INLINE uint32_t fast_function(void) {
    return 42;
}

/* Prevent inlining */
XGL_NOINLINE void debug_function(void) {
    /* ... */
}

/* Packed structure */
typedef struct XGL_PACKED {
    uint8_t a;
    uint32_t b;
} packed_struct_t;

/* Aligned structure */
typedef struct XGL_ALIGNED(16) {
    uint8_t data[16];
} aligned_struct_t;

/* Unused parameter */
void callback(void* XGL_UNUSED user_data) {
    /* ... */
}

/* Weak symbol (can be overridden) */
XGL_WEAK void default_handler(void) {
    /* Default implementation */
}

/* Branch prediction hints */
if (XGL_LIKELY(condition)) {
    /* Common case */
} else {
    /* Rare case */
}

if (XGL_UNLIKELY(error)) {
    /* Error handling */
}
```

## Platform-Specific Considerations

### ARM Cortex-M0/M0+

- **Strict alignment required** - Use byte-wise access for multi-byte fields
- **No unaligned access** - Hardware fault on unaligned access
- **Limited instruction set** - Avoid complex operations

```c
#if defined(XGL_ARCH_ARM_CORTEX_M0)
    /* Use byte-wise access */
    uint32_t value = ((uint32_t)buffer[0]) |
                     ((uint32_t)buffer[1] << 8) |
                     ((uint32_t)buffer[2] << 16) |
                     ((uint32_t)buffer[3] << 24);
#else
    /* Direct access is safe */
    uint32_t value = *(uint32_t*)buffer;
#endif
```

### x86/x86-64

- **Relaxed alignment** - Unaligned access supported
- **Little-endian** - LSB first
- **Fast unaligned access** - Hardware optimized

### Big-Endian Systems

- **Network byte order** - Big-endian is network standard
- **Byte swapping** - May need conversion for protocol compatibility

```c
#if defined(XGL_BIG_ENDIAN)
    /* Swap bytes for little-endian protocol */
    uint32_t network_value = __builtin_bswap32(host_value);
#else
    /* No conversion needed */
    uint32_t network_value = host_value;
#endif
```

## Testing

Run platform detection tests:

```bash
cmake -DXGL_BUILD_TESTS=ON -B build
cmake --build build --target xgl_tests
./build/test/xgl_tests --gtest_filter=XglPlatformTest.*
```

## Requirements Validation

This implementation satisfies the following requirements:

- **Requirement 28.1**: Platform-specific operations abstraction
- **Requirement 28.4**: Compile without warnings on multiple compilers
- **Requirement 28.5**: Support both 32-bit and 64-bit architectures
- **Requirement 51.1**: Detect host endianness at compile time
- **Requirement 52.4**: Detect alignment requirements at compile time

## Supported Platforms

### Tested Platforms

- ✅ Windows (MSVC, x86-64)
- ✅ Linux (GCC, x86-64)
- ✅ macOS (Clang, x86-64, ARM64)
- ✅ ARM Cortex-M0 (GCC, bare-metal)
- ✅ ARM Cortex-M4 (GCC, FreeRTOS)

### Supported But Untested

- RISC-V (32-bit, 64-bit)
- MIPS (32-bit, 64-bit)
- PowerPC (32-bit, 64-bit)
- Zephyr RTOS
- RT-Thread RTOS

## Adding New Platforms

To add support for a new platform:

1. **Add compiler detection** in `xgl_platform.h`:
   ```c
   #if defined(__YOUR_COMPILER__)
       #define XGL_COMPILER_YOUR_COMPILER
       #define XGL_COMPILER_NAME "YourCompiler"
       #define XGL_COMPILER_VERSION __YOUR_VERSION__
   #endif
   ```

2. **Add OS detection**:
   ```c
   #if defined(__YOUR_OS__)
       #define XGL_OS_YOUR_OS
       #define XGL_OS_NAME "YourOS"
   #endif
   ```

3. **Add architecture detection**:
   ```c
   #if defined(__YOUR_ARCH__)
       #define XGL_ARCH_YOUR_ARCH
       #define XGL_ARCH_NAME "YourArch"
   #endif
   ```

4. **Add tests** in `test/test_platform.cpp`

5. **Update this README** with platform details

## API Reference

### Types

| Type | Description |
|------|-------------|
| `xgl_platform_info_t` | Platform information structure |

### Functions

| Function | Description | Returns |
|----------|-------------|---------|
| `xgl_platform_get_info(info)` | Get platform information | void |
| `xgl_platform_info_string(buffer, size)` | Format platform info as string | int (bytes written) |
| `xgl_is_little_endian()` | Check if system is little-endian | int (1=little, 0=big) |
| `xgl_is_aligned(ptr, alignment)` | Check if pointer is aligned | int (1=aligned, 0=not) |
| `xgl_align_up(ptr, alignment)` | Align pointer up | void* |
| `xgl_align_size(size, alignment)` | Align size up | size_t |

### Macros

#### Compiler Detection
- `XGL_COMPILER_GCC` - GCC compiler
- `XGL_COMPILER_CLANG` - Clang compiler
- `XGL_COMPILER_MSVC` - MSVC compiler
- `XGL_COMPILER_NAME` - Compiler name string
- `XGL_COMPILER_VERSION` - Compiler version number

#### OS Detection
- `XGL_OS_WINDOWS` - Windows OS
- `XGL_OS_LINUX` - Linux OS
- `XGL_OS_MACOS` - macOS
- `XGL_OS_FREERTOS` - FreeRTOS
- `XGL_OS_BAREMETAL` - Bare-metal
- `XGL_OS_NAME` - OS name string

#### Architecture Detection
- `XGL_ARCH_ARM` - ARM architecture
- `XGL_ARCH_ARM_CORTEX_M0` - ARM Cortex-M0
- `XGL_ARCH_ARM_CORTEX_M4` - ARM Cortex-M4
- `XGL_ARCH_X86` - x86 32-bit
- `XGL_ARCH_X86_64` - x86-64
- `XGL_ARCH_32BIT` - 32-bit architecture
- `XGL_ARCH_64BIT` - 64-bit architecture
- `XGL_ARCH_NAME` - Architecture name string

#### Endianness Detection
- `XGL_LITTLE_ENDIAN` - Little-endian byte order
- `XGL_BIG_ENDIAN` - Big-endian byte order
- `XGL_ENDIAN_NAME` - Endianness name string

#### Alignment Detection
- `XGL_STRICT_ALIGNMENT` - Strict alignment required
- `XGL_RELAXED_ALIGNMENT` - Relaxed alignment
- `XGL_ALIGNMENT_REQUIRED` - Required alignment in bytes
- `XGL_ALIGNMENT_NAME` - Alignment name string

#### Compiler Attributes
- `XGL_INLINE` - Force inline
- `XGL_NOINLINE` - Prevent inlining
- `XGL_PACKED` - Packed structure
- `XGL_ALIGNED(n)` - Align to n bytes
- `XGL_UNUSED` - Unused parameter
- `XGL_WEAK` - Weak symbol
- `XGL_NORETURN` - Function doesn't return
- `XGL_LIKELY(x)` - Branch likely taken
- `XGL_UNLIKELY(x)` - Branch unlikely taken
- `XGL_MEMORY_BARRIER()` - Memory barrier
- `XGL_PREFETCH(addr)` - Prefetch hint
