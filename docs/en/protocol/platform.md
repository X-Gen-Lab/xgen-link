# Platform Abstraction Layer

The Platform layer provides the portability foundation for XGL across compilers, operating systems, and CPU architectures. Through compile-time detection and runtime verification, it ensures the protocol stack runs correctly on environments ranging from bare-metal MCUs to desktop systems.

## Design Goals

- **Compile-time auto-detection**: Preprocessor macros automatically identify compiler, OS, architecture, endianness, and alignment requirements.
- **Zero-overhead abstraction**: Most platform detection is compile-time with no runtime cost.
- **Portable mutex**: Supports noop (single-thread), POSIX pthread, Windows SRWLOCK, and FreeRTOS semaphore.
- **Injectable time source**: Time provider interface enables deterministic testing and simulation.

## Compiler Detection

| Compiler | Macro | Version Macro |
| --- | --- | --- |
| Clang | `XGL_COMPILER_CLANG` | `XGL_COMPILER_VERSION` |
| GCC | `XGL_COMPILER_GCC` | `XGL_COMPILER_VERSION` |
| MSVC | `XGL_COMPILER_MSVC` | `XGL_COMPILER_VERSION` |
| ARM CC | `XGL_COMPILER_ARM` | `XGL_COMPILER_VERSION` |
| IAR | `XGL_COMPILER_IAR` | `XGL_COMPILER_VERSION` |
| Keil | `XGL_COMPILER_KEIL` | `XGL_COMPILER_VERSION` |

All compilers provide a unified `XGL_COMPILER_NAME` string.

## OS Detection

| OS | Macro | Special Flags |
| --- | --- | --- |
| Windows | `XGL_OS_WINDOWS` | `XGL_OS_WINDOWS_32` / `XGL_OS_WINDOWS_64` |
| Linux | `XGL_OS_LINUX` | `XGL_OS_POSIX` |
| macOS | `XGL_OS_MACOS` | `XGL_OS_POSIX`, `XGL_OS_IOS` (if iOS) |
| Unix | `XGL_OS_UNIX` | `XGL_OS_POSIX` |
| FreeRTOS | `XGL_OS_FREERTOS` | `XGL_OS_RTOS` |
| Zephyr | `XGL_OS_ZEPHYR` | `XGL_OS_RTOS` |
| RT-Thread | `XGL_OS_RTTHREAD` | `XGL_OS_RTOS` |
| Bare-Metal | `XGL_OS_BAREMETAL` | No other OS flag |

Detection order: Windows → Linux → macOS → Unix → FreeRTOS → Zephyr → RT-Thread → Bare-Metal (default fallback).

## Architecture Detection

| Architecture Family | Macro | Sub-variants |
| --- | --- | --- |
| ARM | `XGL_ARCH_ARM` | Cortex-M0/M3/M4/M23/M33, ARM64 |
| x86 | `XGL_ARCH_X86` | — |
| x86-64 | `XGL_ARCH_X86_64` | — |
| RISC-V | `XGL_ARCH_RISCV` | RV32 / RV64 |
| MIPS | `XGL_ARCH_MIPS` | MIPS32 / MIPS64 |
| PowerPC | `XGL_ARCH_POWERPC` | 32 / 64-bit |

Pointer size is also detected: `XGL_ARCH_32BIT` / `XGL_ARCH_64BIT`.

## Endianness Detection

Detection priority:

1. GCC/Clang `__BYTE_ORDER__` macro
2. Windows (always little-endian)
3. Explicit markers (`__LITTLE_ENDIAN__`, `__ARMEL__`, etc.)
4. x86/x64 (always little-endian)
5. Default assumption: little-endian (with compile warning)

Runtime verification via `xgl_is_little_endian()` reads a `uint32_t` to confirm compile-time detection.

!!! note "Wire Format mandates little-endian"
    XGL wire format uses offset-based hand-written encoding (no packed structs). All multi-byte fields are serialized in little-endian order, matching native byte order on x86/ARM.

## Alignment Requirements

| Architecture | Alignment Mode | Requirement |
| --- | --- | --- |
| Cortex-M0, Cortex-M23, MIPS | Strict (`XGL_STRICT_ALIGNMENT`) | 4-byte alignment |
| x86, Cortex-M3/M4/M7 | Relaxed (`XGL_RELAXED_ALIGNMENT`) | 1-byte alignment |

## Compiler Attribute Macros

| Macro | GCC/Clang | MSVC | Purpose |
| --- | --- | --- | --- |
| `XGL_INLINE` | `static inline __attribute__((always_inline))` | `static __forceinline` | Force inline |
| `XGL_NOINLINE` | `__attribute__((noinline))` | `__declspec(noinline)` | Prevent inline |
| `XGL_PACKED` | `__attribute__((packed))` | (use pragma pack) | Struct packing |
| `XGL_ALIGNED(n)` | `__attribute__((aligned(n)))` | `__declspec(align(n))` | Alignment |
| `XGL_LIKELY(x)` | `__builtin_expect(!!(x), 1)` | `(x)` | Branch prediction |
| `XGL_UNLIKELY(x)` | `__builtin_expect(!!(x), 0)` | `(x)` | Branch prediction |
| `XGL_WEAK` | `__attribute__((weak))` | `__declspec(selectany)` | Weak symbol |
| `XGL_NORETURN` | `__attribute__((noreturn))` | `__declspec(noreturn)` | Non-returning function |

## Mutex Three-State

| State | Condition | Implementation | Overhead |
| --- | --- | --- | --- |
| No-op | `XGL_THREAD_SAFE` disabled | All lock/unlock are no-ops | Zero |
| POSIX | Linux/macOS + `XGL_THREAD_SAFE` | `pthread_mutex_t` | Low |
| Windows | Windows + `XGL_THREAD_SAFE` | `CRITICAL_SECTION` | Low |
| FreeRTOS | FreeRTOS + `XGL_THREAD_SAFE` | `SemaphoreHandle_t` | Medium |
| Bare-Metal | No OS + `XGL_THREAD_SAFE` | No-op (single-core assumption) | Zero |

### Mutex Guard (RAII)

```c
// GCC/Clang: automatic unlock
XGL_MUTEX_SCOPED_LOCK(&my_mutex);
// Automatically released at scope exit

// Manual mode
xgl_mutex_guard_t guard = xgl_mutex_guard_lock(&my_mutex);
// ... critical section ...
xgl_mutex_guard_unlock(&guard);
```

## Time Provider

`xgl_time_provider_t` encapsulates a time source for dependency injection, enabling deterministic testing.

| Source | Purpose |
| --- | --- |
| `xgl_time_provider_default()` | System time (POSIX/Windows) |
| `xgl_time_provider_mock()` | Simulated time, manually advanced |

### Mock Time

```c
xgl_mock_time_t mock;
xgl_mock_time_init(&mock, 0);
xgl_time_provider_t tp = xgl_time_provider_mock(&mock);

// Simulate time passage
xgl_mock_time_advance(&mock, 1000);  // +1000ms
```

Mock time makes timeout, retransmission, and other time-dependent protocol behavior deterministically testable.

## Platform Info Query

`xgl_platform_info_t` aggregates all compile-time detection results:

| Field | Type | Description |
| --- | --- | --- |
| `compiler_name` | const char* | Compiler name |
| `os_name` | const char* | OS name |
| `arch_name` | const char* | Architecture name |
| `arch_subname` | const char* | Architecture sub-variant |
| `endian_name` | const char* | Endianness |
| `alignment_name` | const char* | Alignment requirement |
| `pointer_size` | uint8_t | Pointer size (bytes) |
| `is_little_endian` | uint8_t | 1=little-endian |
| `is_64bit` | uint8_t | 1=64-bit |

Use `xgl_platform_get_info()` to fill, `xgl_platform_info_string()` to format as readable string.

## Evidence

| Rule | Source | Test |
| --- | --- | --- |
| Platform detection macros | `include/xgl/internal/xgl_platform.h` | `test/test_platform.cpp` |
| Mutex implementations | `src/platform/xgl_mutex.c`, `xgl_mutex_noop.c`, `xgl_mutex_windows.c` | `test/test_mutex.cpp` |
| Time provider | `src/platform/xgl_time_provider.c` | `test/test_time_provider.cpp` |
| Time functions | `src/platform/xgl_time.c` | `test/test_time.cpp` |
| Platform info | `src/platform/xgl_platform.c` | `test/test_platform.cpp` |