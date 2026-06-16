/**
 * \file            xgl_atomic.h
 * \brief           Atomic operations abstraction layer
 * \author          X-Gen Lab
 */

#ifndef XGL_ATOMIC_H
#define XGL_ATOMIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------*/
/* Platform Detection                                                        */
/*---------------------------------------------------------------------------*/

/* Detect platform for atomic implementation */
#if defined(_WIN32) || defined(_WIN64)
    #define XGL_PLATFORM_WINDOWS
#elif defined(__unix__) || defined(__unix) || defined(__linux__) || defined(__APPLE__)
    #define XGL_PLATFORM_POSIX
#elif defined(__FREERTOS__)
    #define XGL_PLATFORM_FREERTOS
#else
    #define XGL_PLATFORM_BAREMETAL
#endif

/* Detect C11 atomic support */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
    #define XGL_HAS_C11_ATOMICS
#endif

/*---------------------------------------------------------------------------*/
/* Atomic Type Definition                                                    */
/*---------------------------------------------------------------------------*/

#ifdef XGL_THREAD_SAFE

#if defined(XGL_HAS_C11_ATOMICS)
    /* C11 atomic implementation */
    #include <stdatomic.h>
    typedef atomic_uint xgl_atomic_t;
    typedef atomic_int xgl_atomic_int_t;
    typedef atomic_ulong xgl_atomic_ulong_t;
    typedef atomic_bool xgl_atomic_bool_t;

#elif defined(XGL_PLATFORM_WINDOWS)
    /* Windows atomic implementation */
    #include <windows.h>
    typedef volatile LONG xgl_atomic_t;
    typedef volatile LONG xgl_atomic_int_t;
    typedef volatile LONG64 xgl_atomic_ulong_t;
    typedef volatile LONG xgl_atomic_bool_t;

#elif defined(__GNUC__) || defined(__clang__)
    /* GCC/Clang builtin atomics */
    typedef volatile uint32_t xgl_atomic_t;
    typedef volatile int32_t xgl_atomic_int_t;
    typedef volatile uint64_t xgl_atomic_ulong_t;
    typedef volatile uint32_t xgl_atomic_bool_t;

#else
    /* Fallback: non-atomic (requires external synchronization) */
    typedef volatile uint32_t xgl_atomic_t;
    typedef volatile int32_t xgl_atomic_int_t;
    typedef volatile uint64_t xgl_atomic_ulong_t;
    typedef volatile uint32_t xgl_atomic_bool_t;
    #define XGL_ATOMIC_FALLBACK
    #warning "No atomic operations available, using non-atomic fallback"
#endif

#else
    /* Thread safety disabled - no atomic operations needed */
    typedef uint32_t xgl_atomic_t;
    typedef int32_t xgl_atomic_int_t;
    typedef uint64_t xgl_atomic_ulong_t;
    typedef uint32_t xgl_atomic_bool_t;
#endif /* XGL_THREAD_SAFE */

/*---------------------------------------------------------------------------*/
/* Atomic Operations - Unsigned Integer                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize atomic variable
 * \param[out]      ptr: Pointer to atomic variable
 * \param[in]       value: Initial value
 * \note            Must be called before using atomic variable
 */
static inline void xgl_atomic_init(xgl_atomic_t* ptr, uint32_t value) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    atomic_init(ptr, value);
#else
    *ptr = value;
#endif
}

/**
 * \brief           Load atomic variable
 * \param[in]       ptr: Pointer to atomic variable
 * \return          Current value
 */
static inline uint32_t xgl_atomic_load(const xgl_atomic_t* ptr) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_load(ptr);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    return (uint32_t)InterlockedCompareExchange((LONG*)ptr, 0, 0);
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
#else
    return *ptr;
#endif
}

/**
 * \brief           Store value to atomic variable
 * \param[out]      ptr: Pointer to atomic variable
 * \param[in]       value: Value to store
 */
static inline void xgl_atomic_store(xgl_atomic_t* ptr, uint32_t value) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    atomic_store(ptr, value);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    InterlockedExchange((LONG*)ptr, (LONG)value);
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
#else
    *ptr = value;
#endif
}

/**
 * \brief           Atomic increment (post-increment)
 * \param[in,out]   ptr: Pointer to atomic variable
 * \return          Previous value before increment
 */
static inline uint32_t xgl_atomic_fetch_inc(xgl_atomic_t* ptr) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_fetch_add(ptr, 1);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    return (uint32_t)InterlockedIncrement((LONG*)ptr) - 1;
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_fetch_add(ptr, 1, __ATOMIC_SEQ_CST);
#else
    uint32_t old = *ptr;
    (*ptr)++;
    return old;
#endif
}

/**
 * \brief           Atomic decrement (post-decrement)
 * \param[in,out]   ptr: Pointer to atomic variable
 * \return          Previous value before decrement
 */
static inline uint32_t xgl_atomic_fetch_dec(xgl_atomic_t* ptr) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_fetch_sub(ptr, 1);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    return (uint32_t)InterlockedDecrement((LONG*)ptr) + 1;
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_fetch_sub(ptr, 1, __ATOMIC_SEQ_CST);
#else
    uint32_t old = *ptr;
    (*ptr)--;
    return old;
#endif
}

/**
 * \brief           Atomic add
 * \param[in,out]   ptr: Pointer to atomic variable
 * \param[in]       value: Value to add
 * \return          Previous value before addition
 */
static inline uint32_t xgl_atomic_fetch_add(xgl_atomic_t* ptr, uint32_t value) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_fetch_add(ptr, value);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    return (uint32_t)InterlockedExchangeAdd((LONG*)ptr, (LONG)value);
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
#else
    uint32_t old = *ptr;
    *ptr += value;
    return old;
#endif
}

/**
 * \brief           Atomic subtract
 * \param[in,out]   ptr: Pointer to atomic variable
 * \param[in]       value: Value to subtract
 * \return          Previous value before subtraction
 */
static inline uint32_t xgl_atomic_fetch_sub(xgl_atomic_t* ptr, uint32_t value) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_fetch_sub(ptr, value);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    return (uint32_t)InterlockedExchangeAdd((LONG*)ptr, -(LONG)value);
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
#else
    uint32_t old = *ptr;
    *ptr -= value;
    return old;
#endif
}

/**
 * \brief           Atomic compare-and-swap
 * \param[in,out]   ptr: Pointer to atomic variable
 * \param[in,out]   expected: Pointer to expected value (updated on failure)
 * \param[in]       desired: Desired value to set
 * \return          true if swap succeeded, false otherwise
 */
static inline bool xgl_atomic_compare_exchange(xgl_atomic_t* ptr,
                                               uint32_t* expected,
                                               uint32_t desired) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_compare_exchange_strong(ptr, expected, desired);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    LONG old = InterlockedCompareExchange((LONG*)ptr, (LONG)desired, (LONG)*expected);
    if (old == (LONG)*expected) {
        return true;
    } else {
        *expected = (uint32_t)old;
        return false;
    }
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_compare_exchange_n(ptr, expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#else
    if (*ptr == *expected) {
        *ptr = desired;
        return true;
    } else {
        *expected = *ptr;
        return false;
    }
#endif
}

/**
 * \brief           Atomic exchange
 * \param[in,out]   ptr: Pointer to atomic variable
 * \param[in]       value: New value to set
 * \return          Previous value
 */
static inline uint32_t xgl_atomic_exchange(xgl_atomic_t* ptr, uint32_t value) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_exchange(ptr, value);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    return (uint32_t)InterlockedExchange((LONG*)ptr, (LONG)value);
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_exchange_n(ptr, value, __ATOMIC_SEQ_CST);
#else
    uint32_t old = *ptr;
    *ptr = value;
    return old;
#endif
}

/*---------------------------------------------------------------------------*/
/* Atomic Operations - Boolean                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize atomic boolean
 * \param[out]      ptr: Pointer to atomic boolean
 * \param[in]       value: Initial value
 */
static inline void xgl_atomic_bool_init(xgl_atomic_bool_t* ptr, bool value) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    atomic_init(ptr, value);
#else
    *ptr = value ? 1 : 0;
#endif
}

/**
 * \brief           Load atomic boolean
 * \param[in]       ptr: Pointer to atomic boolean
 * \return          Current value
 */
static inline bool xgl_atomic_bool_load(const xgl_atomic_bool_t* ptr) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    return atomic_load(ptr);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    return InterlockedCompareExchange((LONG*)ptr, 0, 0) != 0;
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST) != 0;
#else
    return *ptr != 0;
#endif
}

/**
 * \brief           Store value to atomic boolean
 * \param[out]      ptr: Pointer to atomic boolean
 * \param[in]       value: Value to store
 */
static inline void xgl_atomic_bool_store(xgl_atomic_bool_t* ptr, bool value) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    atomic_store(ptr, value);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    InterlockedExchange((LONG*)ptr, value ? 1 : 0);
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    __atomic_store_n(ptr, value ? 1 : 0, __ATOMIC_SEQ_CST);
#else
    *ptr = value ? 1 : 0;
#endif
}

/*---------------------------------------------------------------------------*/
/* Convenience Macros                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Atomic increment (returns new value)
 */
#define xgl_atomic_inc(ptr) (xgl_atomic_fetch_inc(ptr) + 1)

/**
 * \brief           Atomic decrement (returns new value)
 */
#define xgl_atomic_dec(ptr) (xgl_atomic_fetch_dec(ptr) - 1)

/**
 * \brief           Atomic add (returns new value)
 */
#define xgl_atomic_add(ptr, val) (xgl_atomic_fetch_add(ptr, val) + (val))

/**
 * \brief           Atomic subtract (returns new value)
 */
#define xgl_atomic_sub(ptr, val) (xgl_atomic_fetch_sub(ptr, val) - (val))

/*---------------------------------------------------------------------------*/
/* Memory Barriers (Optional)                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Full memory barrier
 * \note            Ensures all memory operations complete before continuing
 */
static inline void xgl_atomic_fence(void) {
#if defined(XGL_THREAD_SAFE) && defined(XGL_HAS_C11_ATOMICS)
    atomic_thread_fence(memory_order_seq_cst);
#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)
    MemoryBarrier();
#elif defined(XGL_THREAD_SAFE) && (defined(__GNUC__) || defined(__clang__))
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    /* No-op for non-threaded builds */
    (void)0;
#endif
}

/**
 * \brief           Compiler barrier (prevents reordering)
 * \note            Does not emit memory barrier instructions
 */
static inline void xgl_atomic_compiler_barrier(void) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    /* No-op */
    (void)0;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_ATOMIC_H */
