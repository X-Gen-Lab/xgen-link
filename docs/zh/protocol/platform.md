# Platform 抽象层

Platform 层为 XGL 提供跨编译器、操作系统和 CPU 架构的可移植性基础。它通过编译时检测和运行时验证,确保协议栈在从裸机 MCU 到桌面系统的各种环境中正确运行。

## 设计目标

- **编译时自动检测**:通过预处理宏自动识别编译器、OS、架构、字节序和对齐要求。
- **零开销抽象**:大部分 platform 检测在编译时完成,不产生运行时开销。
- **可移植 mutex**:支持 noop(单线程)、POSIX pthread、Windows SRWLOCK 和 FreeRTOS semaphore。
- **可注入时间源**:通过 time provider 接口支持确定性测试和模拟。

## 编译器检测

| 编译器 | 宏 | 版本宏 |
| --- | --- | --- |
| Clang | `XGL_COMPILER_CLANG` | `XGL_COMPILER_VERSION` |
| GCC | `XGL_COMPILER_GCC` | `XGL_COMPILER_VERSION` |
| MSVC | `XGL_COMPILER_MSVC` | `XGL_COMPILER_VERSION` |
| ARM CC | `XGL_COMPILER_ARM` | `XGL_COMPILER_VERSION` |
| IAR | `XGL_COMPILER_IAR` | `XGL_COMPILER_VERSION` |
| Keil | `XGL_COMPILER_KEIL` | `XGL_COMPILER_VERSION` |

所有编译器统一提供 `XGL_COMPILER_NAME` 字符串。

## 操作系统检测

| OS | 宏 | 特殊标记 |
| --- | --- | --- |
| Windows | `XGL_OS_WINDOWS` | `XGL_OS_WINDOWS_32` / `XGL_OS_WINDOWS_64` |
| Linux | `XGL_OS_LINUX` | `XGL_OS_POSIX` |
| macOS | `XGL_OS_MACOS` | `XGL_OS_POSIX`, `XGL_OS_IOS` (if iOS) |
| Unix | `XGL_OS_UNIX` | `XGL_OS_POSIX` |
| FreeRTOS | `XGL_OS_FREERTOS` | `XGL_OS_RTOS` |
| Zephyr | `XGL_OS_ZEPHYR` | `XGL_OS_RTOS` |
| RT-Thread | `XGL_OS_RTTHREAD` | `XGL_OS_RTOS` |
| Bare-Metal | `XGL_OS_BAREMETAL` | 无其他 OS 标记 |

检测顺序:Windows → Linux → macOS → Unix → FreeRTOS → Zephyr → RT-Thread → Bare-Metal(默认回退)。

## 架构检测

| 架构族 | 宏 | 子型号 |
| --- | --- | --- |
| ARM | `XGL_ARCH_ARM` | Cortex-M0/M3/M4/M23/M33, ARM64 |
| x86 | `XGL_ARCH_X86` | — |
| x86-64 | `XGL_ARCH_X86_64` | — |
| RISC-V | `XGL_ARCH_RISCV` | RV32 / RV64 |
| MIPS | `XGL_ARCH_MIPS` | MIPS32 / MIPS64 |
| PowerPC | `XGL_ARCH_POWERPC` | 32 / 64 位 |

同时检测指针大小: `XGL_ARCH_32BIT` / `XGL_ARCH_64BIT`。

## 字节序检测

检测优先级:

1. GCC/Clang `__BYTE_ORDER__` 宏
2. Windows (始终小端)
3. 显式标记 (`__LITTLE_ENDIAN__`, `__ARMEL__` 等)
4. x86/x64 (始终小端)
5. 默认假设小端(附编译警告)

运行时验证函数 `xgl_is_little_endian()` 通过读取 `uint32_t` 的字节序确认编译时检测的正确性。

!!! note "Wire Format 强制小端序"
    XGL wire format 使用偏移量手写编码(不依赖 packed struct),所有多字节字段以小端序序列化。这与 x86/ARM 的原生字节序一致,在大端序平台上需要逐字段转换。

## 对齐要求检测

| 架构 | 对齐模式 | 要求 |
| --- | --- | --- |
| Cortex-M0, Cortex-M23, MIPS | 严格对齐 (`XGL_STRICT_ALIGNMENT`) | 4 字节对齐 |
| x86, Cortex-M3/M4/M7 | 宽松对齐 (`XGL_RELAXED_ALIGNMENT`) | 1 字节对齐 |

**对齐工具函数:**

| 函数 | 说明 |
| --- | --- |
| `xgl_is_aligned(ptr, alignment)` | 检查指针是否对齐 |
| `xgl_align_up(ptr, alignment)` | 将指针向上对齐 |
| `xgl_align_size(size, alignment)` | 将大小向上对齐 |

## 编译器属性宏

| 宏 | GCC/Clang | MSVC | 含义 |
| --- | --- | --- | --- |
| `XGL_INLINE` | `static inline __attribute__((always_inline))` | `static __forceinline` | 强制内联 |
| `XGL_NOINLINE` | `__attribute__((noinline))` | `__declspec(noinline)` | 禁止内联 |
| `XGL_PACKED` | `__attribute__((packed))` | (使用 pragma pack) | 结构体紧凑排列 |
| `XGL_ALIGNED(n)` | `__attribute__((aligned(n)))` | `__declspec(align(n))` | 指定对齐 |
| `XGL_UNUSED` | `__attribute__((unused))` | — | 抑制未使用警告 |
| `XGL_WEAK` | `__attribute__((weak))` | `__declspec(selectany)` | 弱符号 |
| `XGL_NORETURN` | `__attribute__((noreturn))` | `__declspec(noreturn)` | 不返回函数 |
| `XGL_LIKELY(x)` | `__builtin_expect(!!(x), 1)` | `(x)` | 分支预测提示 |
| `XGL_UNLIKELY(x)` | `__builtin_expect(!!(x), 0)` | `(x)` | 分支预测提示 |

## 内存屏障

```c
// GCC/Clang
#define XGL_MEMORY_BARRIER() __asm__ __volatile__("" ::: "memory")

// MSVC
#define XGL_MEMORY_BARRIER() _ReadWriteBarrier()
```

`XGL_MEMORY_BARRIER()` 是编译器级屏障,阻止编译器重排内存访问。在单核 MCU 上足够;多核系统需要平台特定的硬件屏障。

## Mutex 三态

XGL 的 mutex 根据编译配置有三种实现:

| 状态 | 条件 | 实现 | 开销 |
| --- | --- | --- | --- |
| No-op | `XGL_THREAD_SAFE` 未启用 | 空操作,所有 lock/unlock 是 no-op | 零 |
| POSIX | Linux/macOS + `XGL_THREAD_SAFE` | `pthread_mutex_t` | 低 |
| Windows | Windows + `XGL_THREAD_SAFE` | `CRITICAL_SECTION` | 低 |
| FreeRTOS | FreeRTOS + `XGL_THREAD_SAFE` | `SemaphoreHandle_t` | 中 |
| Bare-Metal | 无 OS + `XGL_THREAD_SAFE` | No-op (单核假设) | 零 |

### Mutex API

| 函数 | 说明 |
| --- | --- |
| `xgl_mutex_init()` | 初始化 mutex |
| `xgl_mutex_lock()` | 阻塞获取锁 |
| `xgl_mutex_trylock()` | 非阻塞尝试获取锁 |
| `xgl_mutex_unlock()` | 释放锁 |
| `xgl_mutex_destroy()` | 销毁 mutex |

### Mutex Guard (RAII)

```c
// GCC/Clang: 自动解锁
XGL_MUTEX_SCOPED_LOCK(&my_mutex);
// 作用域结束时自动释放

// 手动模式
xgl_mutex_guard_t guard = xgl_mutex_guard_lock(&my_mutex);
// ... critical section ...
xgl_mutex_guard_unlock(&guard);
```

## Time Provider

`xgl_time_provider_t` 封装时间源,支持依赖注入用于确定性测试。

```text
xgl_time_provider_t
├── get_time_ms (xgl_time_provider_fn — 返回毫秒时间戳)
└── user_data   (void* — 上下文)
```

**内置时间源:**

| 时间源 | 用途 |
| --- | --- |
| `xgl_time_provider_default()` | 系统时间(POSIX/Windows) |
| `xgl_time_provider_mock()` | 模拟时间,手动推进 |

**Mock 时间:**

```c
xgl_mock_time_t mock;
xgl_mock_time_init(&mock, 0);
xgl_time_provider_t tp = xgl_time_provider_mock(&mock);

// 模拟时间流逝
xgl_mock_time_advance(&mock, 1000);  // +1000ms
```

Mock time 使协议栈的超时、重传等时间依赖行为可以确定性地测试。

## 平台信息查询

`xgl_platform_info_t` 汇总所有编译时检测结果:

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `compiler_name` | const char* | 编译器名称 |
| `compiler_version` | uint32_t | 编译器版本 |
| `os_name` | const char* | 操作系统名称 |
| `arch_name` | const char* | 架构名称 |
| `arch_subname` | const char* | 架构子型号 |
| `endian_name` | const char* | 字节序 |
| `alignment_name` | const char* | 对齐要求 |
| `pointer_size` | uint8_t | 指针大小(字节) |
| `alignment_required` | uint8_t | 最小对齐要求(字节) |
| `is_little_endian` | uint8_t | 1=小端, 0=大端 |
| `is_64bit` | uint8_t | 1=64位, 0=32位 |

通过 `xgl_platform_get_info()` 填充, `xgl_platform_info_string()` 格式化为可读字符串。

## 证据

| 规则 | 源码 | 测试 |
| --- | --- | --- |
| 平台检测宏 | `include/xgl/internal/xgl_platform.h` | `test/test_platform.cpp` |
| Mutex 实现 | `src/platform/xgl_mutex.c`, `xgl_mutex_noop.c`, `xgl_mutex_windows.c` | `test/test_mutex.cpp` |
| Time provider | `src/platform/xgl_time_provider.c` | `test/test_time_provider.cpp` |
| Time functions | `src/platform/xgl_time.c` | `test/test_time.cpp` |
| Platform info | `src/platform/xgl_platform.c` | `test/test_platform.cpp` |