/**
 * \file            xgl_platform.h
 * \brief           Platform detection and configuration
 * \author          X-Gen Lab
 */

#ifndef XGL_PLATFORM_H
#define XGL_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* Compiler Detection                                                        */
/*---------------------------------------------------------------------------*/

/* Clang Compiler */
#if defined(__clang__)
    #define XGL_COMPILER_CLANG
    #define XGL_COMPILER_NAME "Clang"
    #define XGL_COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)

/* GCC Compiler */
#elif defined(__GNUC__)
    #define XGL_COMPILER_GCC
    #define XGL_COMPILER_NAME "GCC"
    #define XGL_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

/* MSVC Compiler */
#elif defined(_MSC_VER)
    #define XGL_COMPILER_MSVC
    #define XGL_COMPILER_NAME "MSVC"
    #define XGL_COMPILER_VERSION _MSC_VER

/* ARM Compiler */
#elif defined(__ARMCC_VERSION)
    #define XGL_COMPILER_ARM
    #define XGL_COMPILER_NAME "ARM"
    #define XGL_COMPILER_VERSION __ARMCC_VERSION

/* IAR Compiler */
#elif defined(__IAR_SYSTEMS_ICC__)
    #define XGL_COMPILER_IAR
    #define XGL_COMPILER_NAME "IAR"
    #define XGL_COMPILER_VERSION __VER__

/* Keil Compiler */
#elif defined(__CC_ARM)
    #define XGL_COMPILER_KEIL
    #define XGL_COMPILER_NAME "Keil"
    #define XGL_COMPILER_VERSION __ARMCC_VERSION
#endif

/* Default compiler name if not detected */
#ifndef XGL_COMPILER_NAME
    #define XGL_COMPILER_NAME "Unknown"
    #define XGL_COMPILER_VERSION 0
#endif

/*---------------------------------------------------------------------------*/
/* Operating System Detection                                                */
/*---------------------------------------------------------------------------*/

/* Windows */
#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
    #define XGL_OS_WINDOWS
    #define XGL_OS_NAME "Windows"
    #if defined(_WIN64)
        #define XGL_OS_WINDOWS_64
    #else
        #define XGL_OS_WINDOWS_32
    #endif
#endif

/* Linux */
#if defined(__linux__) || defined(__linux)
    #define XGL_OS_LINUX
    #define XGL_OS_NAME "Linux"
    #define XGL_OS_POSIX
#endif

/* macOS */
#if defined(__APPLE__) && defined(__MACH__)
    #define XGL_OS_MACOS
    #define XGL_OS_NAME "macOS"
    #define XGL_OS_POSIX
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define XGL_OS_IOS
    #endif
#endif

/* Unix-like systems */
#if defined(__unix__) || defined(__unix)
    #define XGL_OS_UNIX
    #ifndef XGL_OS_NAME
        #define XGL_OS_NAME "Unix"
    #endif
    #define XGL_OS_POSIX
#endif

/* FreeRTOS */
#if defined(__FREERTOS__) || defined(INC_FREERTOS_H)
    #define XGL_OS_FREERTOS
    #define XGL_OS_NAME "FreeRTOS"
    #define XGL_OS_RTOS
#endif

/* Zephyr RTOS */
#if defined(__ZEPHYR__)
    #define XGL_OS_ZEPHYR
    #define XGL_OS_NAME "Zephyr"
    #define XGL_OS_RTOS
#endif

/* RT-Thread */
#if defined(__RTTHREAD__)
    #define XGL_OS_RTTHREAD
    #define XGL_OS_NAME "RT-Thread"
    #define XGL_OS_RTOS
#endif

/* Bare-Metal (no OS) */
#if !defined(XGL_OS_WINDOWS) && !defined(XGL_OS_LINUX) && \
    !defined(XGL_OS_MACOS) && !defined(XGL_OS_UNIX) && \
    !defined(XGL_OS_FREERTOS) && !defined(XGL_OS_ZEPHYR) && \
    !defined(XGL_OS_RTTHREAD)
    #define XGL_OS_BAREMETAL
    #define XGL_OS_NAME "Bare-Metal"
#endif

/*---------------------------------------------------------------------------*/
/* Architecture Detection                                                    */
/*---------------------------------------------------------------------------*/

/* ARM Architecture */
#if defined(__arm__) || defined(__ARM__) || defined(__ARM_ARCH) || \
    defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
    #define XGL_ARCH_ARM
    #define XGL_ARCH_NAME "ARM"

    /* ARM Cortex-M series */
    #if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_6SM__)
        #define XGL_ARCH_ARM_CORTEX_M0
        #define XGL_ARCH_SUBNAME "Cortex-M0"
    #elif defined(__ARM_ARCH_7M__)
        #define XGL_ARCH_ARM_CORTEX_M3
        #define XGL_ARCH_SUBNAME "Cortex-M3"
    #elif defined(__ARM_ARCH_7EM__)
        #define XGL_ARCH_ARM_CORTEX_M4
        #define XGL_ARCH_SUBNAME "Cortex-M4"
    #elif defined(__ARM_ARCH_8M_BASE__)
        #define XGL_ARCH_ARM_CORTEX_M23
        #define XGL_ARCH_SUBNAME "Cortex-M23"
    #elif defined(__ARM_ARCH_8M_MAIN__)
        #define XGL_ARCH_ARM_CORTEX_M33
        #define XGL_ARCH_SUBNAME "Cortex-M33"
    #endif

    /* ARM 64-bit */
    #if defined(__aarch64__) || defined(_M_ARM64)
        #define XGL_ARCH_ARM64
        #define XGL_ARCH_64BIT
    #else
        #define XGL_ARCH_ARM32
        #define XGL_ARCH_32BIT
    #endif
#endif

/* x86 Architecture */
#if defined(__i386__) || defined(__i386) || defined(_M_IX86) || \
    defined(__X86__) || defined(_X86_)
    #define XGL_ARCH_X86
    #define XGL_ARCH_NAME "x86"
    #define XGL_ARCH_32BIT
#endif

/* x86-64 Architecture */
#if defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || \
    defined(__amd64) || defined(_M_X64) || defined(_M_AMD64)
    #define XGL_ARCH_X86_64
    #define XGL_ARCH_NAME "x86-64"
    #define XGL_ARCH_64BIT
#endif

/* RISC-V Architecture */
#if defined(__riscv) || defined(__riscv__)
    #define XGL_ARCH_RISCV
    #define XGL_ARCH_NAME "RISC-V"
    #if __riscv_xlen == 64
        #define XGL_ARCH_RISCV64
        #define XGL_ARCH_64BIT
    #else
        #define XGL_ARCH_RISCV32
        #define XGL_ARCH_32BIT
    #endif
#endif

/* MIPS Architecture */
#if defined(__mips__) || defined(__mips) || defined(__MIPS__)
    #define XGL_ARCH_MIPS
    #define XGL_ARCH_NAME "MIPS"
    #if defined(__mips64) || defined(__mips64__)
        #define XGL_ARCH_MIPS64
        #define XGL_ARCH_64BIT
    #else
        #define XGL_ARCH_MIPS32
        #define XGL_ARCH_32BIT
    #endif
#endif

/* PowerPC Architecture */
#if defined(__powerpc__) || defined(__powerpc) || defined(__PPC__) || \
    defined(__ppc__) || defined(_M_PPC)
    #define XGL_ARCH_POWERPC
    #define XGL_ARCH_NAME "PowerPC"
    #if defined(__powerpc64__) || defined(__ppc64__)
        #define XGL_ARCH_POWERPC64
        #define XGL_ARCH_64BIT
    #else
        #define XGL_ARCH_POWERPC32
        #define XGL_ARCH_32BIT
    #endif
#endif

/* Default architecture name if not detected */
#ifndef XGL_ARCH_NAME
    #define XGL_ARCH_NAME "Unknown"
#endif

#ifndef XGL_ARCH_SUBNAME
    #define XGL_ARCH_SUBNAME ""
#endif

/* Determine pointer size if not already set */
#if !defined(XGL_ARCH_32BIT) && !defined(XGL_ARCH_64BIT)
    #if defined(__SIZEOF_POINTER__)
        #if __SIZEOF_POINTER__ == 8
            #define XGL_ARCH_64BIT
        #elif __SIZEOF_POINTER__ == 4
            #define XGL_ARCH_32BIT
        #endif
    #elif defined(_WIN64) || defined(__LP64__) || defined(_LP64)
        #define XGL_ARCH_64BIT
    #else
        #define XGL_ARCH_32BIT
    #endif
#endif

/*---------------------------------------------------------------------------*/
/* Endianness Detection                                                      */
/*---------------------------------------------------------------------------*/

/* Try to detect endianness at compile time */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    defined(__ORDER_BIG_ENDIAN__)
    /* GCC/Clang style */
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define XGL_LITTLE_ENDIAN
        #define XGL_ENDIAN_NAME "Little-Endian"
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define XGL_BIG_ENDIAN
        #define XGL_ENDIAN_NAME "Big-Endian"
    #endif
#elif defined(_WIN32) || defined(_WIN64)
    /* Windows is always little-endian */
    #define XGL_LITTLE_ENDIAN
    #define XGL_ENDIAN_NAME "Little-Endian"
#elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || \
      defined(__AARCH64EL__) || defined(_MIPSEL) || defined(__MIPSEL) || \
      defined(__MIPSEL__)
    /* Explicit little-endian markers */
    #define XGL_LITTLE_ENDIAN
    #define XGL_ENDIAN_NAME "Little-Endian"
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || \
      defined(__AARCH64EB__) || defined(_MIPSEB) || defined(__MIPSEB) || \
      defined(__MIPSEB__)
    /* Explicit big-endian markers */
    #define XGL_BIG_ENDIAN
    #define XGL_ENDIAN_NAME "Big-Endian"
#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || \
      defined(_M_X64) || defined(_M_AMD64)
    /* x86/x64 is always little-endian */
    #define XGL_LITTLE_ENDIAN
    #define XGL_ENDIAN_NAME "Little-Endian"
#else
    /* Default to little-endian (most common) */
    #define XGL_LITTLE_ENDIAN
    #define XGL_ENDIAN_NAME "Little-Endian (assumed)"
    #warning "Endianness could not be detected, assuming little-endian"
#endif

/**
 * \brief           Runtime endianness check
 * \return          1 if little-endian, 0 if big-endian
 * \note            Can be used to verify compile-time detection
 */
static inline int xgl_is_little_endian(void) {
    volatile uint32_t test = 0x01020304;
    return (*((volatile uint8_t*)&test) == 0x04);
}

/*---------------------------------------------------------------------------*/
/* Alignment Requirements Detection                                          */
/*---------------------------------------------------------------------------*/

/* Strict alignment architectures (require aligned access) */
#if defined(XGL_ARCH_ARM_CORTEX_M0) || defined(XGL_ARCH_ARM_CORTEX_M23) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_6SM__) || \
    defined(__ARM_ARCH_8M_BASE__)
    #define XGL_STRICT_ALIGNMENT
    #define XGL_ALIGNMENT_REQUIRED 4
    #define XGL_ALIGNMENT_NAME "Strict (4-byte)"
#elif defined(XGL_ARCH_ARM) && !defined(__ARM_FEATURE_UNALIGNED)
    /* ARM without unaligned access support */
    #define XGL_STRICT_ALIGNMENT
    #define XGL_ALIGNMENT_REQUIRED 4
    #define XGL_ALIGNMENT_NAME "Strict (4-byte)"
#elif defined(XGL_ARCH_MIPS)
    /* MIPS typically requires alignment */
    #define XGL_STRICT_ALIGNMENT
    #define XGL_ALIGNMENT_REQUIRED 4
    #define XGL_ALIGNMENT_NAME "Strict (4-byte)"
#else
    /* Relaxed alignment (x86, ARM Cortex-M3/M4/M7, etc.) */
    #define XGL_RELAXED_ALIGNMENT
    #define XGL_ALIGNMENT_REQUIRED 1
    #define XGL_ALIGNMENT_NAME "Relaxed"
#endif

/**
 * \brief           Check if pointer is aligned
 * \param[in]       ptr: Pointer to check
 * \param[in]       alignment: Required alignment (power of 2)
 * \return          1 if aligned, 0 otherwise
 */
static inline int xgl_is_aligned(const void* ptr, size_t alignment) {
    return ((uintptr_t)ptr & (alignment - 1)) == 0;
}

/**
 * \brief           Align pointer up to specified alignment
 * \param[in]       ptr: Pointer to align
 * \param[in]       alignment: Required alignment (power of 2)
 * \return          Aligned pointer
 */
static inline void* xgl_align_up(const void* ptr, size_t alignment) {
    uintptr_t addr = (uintptr_t)ptr;
    return (void*)((addr + alignment - 1) & ~(alignment - 1));
}

/**
 * \brief           Align size up to specified alignment
 * \param[in]       size: Size to align
 * \param[in]       alignment: Required alignment (power of 2)
 * \return          Aligned size
 */
static inline size_t xgl_align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/*---------------------------------------------------------------------------*/
/* Compiler Attributes and Hints                                             */
/*---------------------------------------------------------------------------*/

/* Function attributes */
#if defined(XGL_COMPILER_GCC) || defined(XGL_COMPILER_CLANG)
    #define XGL_INLINE          static inline __attribute__((always_inline))
    #define XGL_NOINLINE        __attribute__((noinline))
    #define XGL_PACKED          __attribute__((packed))
    #define XGL_ALIGNED(n)      __attribute__((aligned(n)))
    #define XGL_UNUSED          __attribute__((unused))
    #define XGL_WEAK            __attribute__((weak))
    #define XGL_NORETURN        __attribute__((noreturn))
    #define XGL_LIKELY(x)       __builtin_expect(!!(x), 1)
    #define XGL_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#elif defined(XGL_COMPILER_MSVC)
    #define XGL_INLINE          static __forceinline
    #define XGL_NOINLINE        __declspec(noinline)
    #define XGL_PACKED          /* MSVC uses #pragma pack */
    #define XGL_ALIGNED(n)      __declspec(align(n))
    #define XGL_UNUSED          /* No equivalent */
    #define XGL_WEAK            __declspec(selectany)
    #define XGL_NORETURN        __declspec(noreturn)
    #define XGL_LIKELY(x)       (x)
    #define XGL_UNLIKELY(x)     (x)
#else
    #define XGL_INLINE          static inline
    #define XGL_NOINLINE        /* No equivalent */
    #define XGL_PACKED          /* No equivalent */
    #define XGL_ALIGNED(n)      /* No equivalent */
    #define XGL_UNUSED          /* No equivalent */
    #define XGL_WEAK            /* No equivalent */
    #define XGL_NORETURN        /* No equivalent */
    #define XGL_LIKELY(x)       (x)
    #define XGL_UNLIKELY(x)     (x)
#endif

/* Memory barriers */
#if defined(XGL_COMPILER_GCC) || defined(XGL_COMPILER_CLANG)
    #define XGL_MEMORY_BARRIER()    __asm__ __volatile__("" ::: "memory")
#elif defined(XGL_COMPILER_MSVC)
    #define XGL_MEMORY_BARRIER()    _ReadWriteBarrier()
#else
    #define XGL_MEMORY_BARRIER()    /* No equivalent */
#endif

/* Prefetch hints */
#if defined(XGL_COMPILER_GCC) || defined(XGL_COMPILER_CLANG)
    #define XGL_PREFETCH(addr)      __builtin_prefetch(addr)
#else
    #define XGL_PREFETCH(addr)      ((void)0)
#endif

/*---------------------------------------------------------------------------*/
/* Platform Information Structure                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Platform information structure
 */
typedef struct {
    const char* compiler_name;      /**< Compiler name */
    uint32_t compiler_version;      /**< Compiler version */
    const char* os_name;            /**< Operating system name */
    const char* arch_name;          /**< Architecture name */
    const char* arch_subname;       /**< Architecture sub-name */
    const char* endian_name;        /**< Endianness name */
    const char* alignment_name;     /**< Alignment requirements */
    uint8_t pointer_size;           /**< Pointer size in bytes */
    uint8_t alignment_required;     /**< Required alignment in bytes */
    uint8_t is_little_endian;       /**< 1 if little-endian, 0 if big-endian */
    uint8_t is_64bit;               /**< 1 if 64-bit, 0 if 32-bit */
} xgl_platform_info_t;

/**
 * \brief           Get platform information
 * \param[out]      info: Pointer to platform info structure
 * \note            Fills structure with compile-time detected information
 */
static inline void xgl_platform_get_info(xgl_platform_info_t* info) {
    if (info == NULL) {
        return;
    }

    info->compiler_name = XGL_COMPILER_NAME;
    info->compiler_version = XGL_COMPILER_VERSION;
    info->os_name = XGL_OS_NAME;
    info->arch_name = XGL_ARCH_NAME;
    info->arch_subname = XGL_ARCH_SUBNAME;
    info->endian_name = XGL_ENDIAN_NAME;
    info->alignment_name = XGL_ALIGNMENT_NAME;
    info->pointer_size = (uint8_t)sizeof(void*);
    info->alignment_required = XGL_ALIGNMENT_REQUIRED;
    info->is_little_endian = (uint8_t)xgl_is_little_endian();

#if defined(XGL_ARCH_64BIT)
    info->is_64bit = 1;
#else
    info->is_64bit = 0;
#endif
}

/**
 * \brief           Print platform information to string
 * \param[out]      buffer: Output buffer
 * \param[in]       size: Buffer size
 * \return          Number of characters written (excluding null terminator)
 */
int xgl_platform_info_string(char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XGL_PLATFORM_H */
