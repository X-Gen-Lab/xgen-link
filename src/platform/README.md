# Platform Abstraction Layer

This directory contains platform-specific implementations for the x_gen_link protocol stack.

## Mutex Abstraction (xgl_mutex.h/c)

The mutex abstraction provides thread-safe synchronization primitives across multiple platforms.

### Supported Platforms

1. **POSIX** (Linux, macOS, Unix)
   - Uses `pthread_mutex_t`
   - Recursive mutex support
   - Detected via: `__unix__`, `__linux__`, `__APPLE__`

2. **Windows**
   - Uses `CRITICAL_SECTION`
   - Recursive by default
   - Detected via: `_WIN32`, `_WIN64`

3. **FreeRTOS**
   - Uses `SemaphoreHandle_t` (recursive mutex)
   - Static allocation for deterministic behavior
   - Detected via: `__FREERTOS__`

4. **Bare-Metal**
   - No-op implementation (zero overhead)
   - For single-threaded environments
   - Default when no platform is detected

### Usage

#### Basic Usage

```c
#include <xgl/xgl_mutex.h>

xgl_mutex_t mutex;

/* Initialize */
xgl_mutex_init(&mutex);

/* Lock */
xgl_mutex_lock(&mutex);

/* Critical section */
/* ... */

/* Unlock */
xgl_mutex_unlock(&mutex);

/* Cleanup */
xgl_mutex_destroy(&mutex);
```

#### Try Lock (Non-Blocking)

```c
if (xgl_mutex_trylock(&mutex) == XGL_OK) {
    /* Got the lock */
    /* ... */
    xgl_mutex_unlock(&mutex);
} else {
    /* Lock was busy */
}
```

#### Lock Guard (RAII-style)

```c
xgl_mutex_guard_t guard = xgl_mutex_guard_lock(&mutex);

/* Critical section */
/* ... */

/* Automatic unlock */
xgl_mutex_guard_unlock(&guard);
```

#### Scoped Lock (GCC/Clang only)

```c
void my_function(void) {
    XGL_MUTEX_SCOPED_LOCK(&mutex);
    
    /* Critical section */
    /* Mutex automatically unlocked when function returns */
}
```

### Compile-Time Configuration

Enable thread safety in Kconfig:

```kconfig
config XGL_THREAD_SAFE
    bool "Enable thread safety"
    default n
```

Or define in CMake:

```cmake
target_compile_definitions(xgl PRIVATE XGL_THREAD_SAFE)
```

### Platform Detection

The implementation automatically detects the platform at compile time:

- `XGL_PLATFORM_WINDOWS` - Windows
- `XGL_PLATFORM_POSIX` - POSIX systems
- `XGL_PLATFORM_FREERTOS` - FreeRTOS
- `XGL_PLATFORM_BAREMETAL` - Bare-metal (default)

### Recursive Locking

All implementations support recursive locking (same thread can lock multiple times):

```c
xgl_mutex_lock(&mutex);
xgl_mutex_lock(&mutex);  /* OK - recursive */
xgl_mutex_unlock(&mutex);
xgl_mutex_unlock(&mutex);
```

### Error Handling

All functions return `xgl_error_t`:

- `XGL_OK` - Success
- `XGL_ERR_NULL_POINTER` - NULL pointer passed
- `XGL_ERR_NOT_INITIALIZED` - Mutex not initialized
- `XGL_ERR_ALREADY_INITIALIZED` - Mutex already initialized
- `XGL_ERR_BUSY` - Lock is busy (trylock only)
- `XGL_ERR_NO_MEMORY` - Memory allocation failed

### Performance

- **Bare-metal**: Zero overhead (no-op)
- **POSIX**: ~50-100 CPU cycles per lock/unlock
- **Windows**: ~30-50 CPU cycles per lock/unlock
- **FreeRTOS**: ~100-200 CPU cycles per lock/unlock

### Testing

Run mutex tests:

```bash
cmake -DXGL_BUILD_TESTS=ON -B build
cmake --build build --target xgl_tests
./build/test/xgl_tests --gtest_filter=XglMutexTest.*
```

### Requirements Validation

This implementation satisfies the following requirements:

- **Requirement 9.1**: Thread-safe protection with mutexes
- **Requirement 9.2**: Atomic operations for reference counting
- **Requirement 9.3**: No synchronization overhead when disabled
- **Requirement 9.5**: Error handling for lock failures
- **Requirement 46.1**: Bare-metal support without RTOS dependencies

## Time Abstraction (xgl_time.h/c)

The time abstraction provides platform-independent timing operations for the x_gen_link protocol stack.

### Supported Platforms

1. **POSIX** (Linux, macOS, Unix)
   - Uses `clock_gettime(CLOCK_MONOTONIC)`
   - Microsecond precision available
   - Detected via: `__unix__`, `__linux__`, `__APPLE__`

2. **Windows**
   - Uses `QueryPerformanceCounter()` for high-resolution timing
   - Microsecond precision available
   - Detected via: `_WIN32`, `_WIN64`

3. **FreeRTOS**
   - Uses `xTaskGetTickCount()` for millisecond timing
   - Hardware timer support via FreeRTOS timers
   - Detected via: `__FREERTOS__`

4. **Bare-Metal**
   - Weak symbols that can be overridden by user
   - Custom time source support
   - Default when no platform is detected

### Usage

#### Basic Time Functions

```c
#include <xgl/xgl_time.h>

/* Get current time in milliseconds */
uint32_t now = xgl_time_ms();

/* Get current time in microseconds (if available) */
uint32_t now_us = xgl_time_us();

/* Delay for specified milliseconds */
xgl_delay_ms(100);

/* Delay for specified microseconds */
xgl_delay_us(1000);
```

#### Elapsed Time Calculation

```c
/* Start timing */
uint32_t start = xgl_time_ms();

/* Do some work */
/* ... */

/* Calculate elapsed time (handles wraparound) */
uint32_t elapsed = xgl_time_elapsed_ms(start);
```

#### Timeout Detection

```c
uint32_t start = xgl_time_ms();
uint32_t timeout_ms = 1000;

while (!xgl_time_is_timeout(start, timeout_ms)) {
    /* Do work with timeout */
    if (work_complete()) {
        break;
    }
}
```

#### Custom Time Source

For testing or custom hardware:

```c
uint32_t my_time_source(void) {
    /* Return current time from custom hardware timer */
    return read_hardware_timer();
}

/* Set custom time source */
xgl_time_set_source(my_time_source);

/* Now xgl_time_ms() uses your custom source */
uint32_t now = xgl_time_ms();

/* Reset to default */
xgl_time_set_source(NULL);
```

#### Hardware Timers (FreeRTOS only)

```c
void timer_callback(void* user_data) {
    /* Called periodically from timer interrupt */
    int* counter = (int*)user_data;
    (*counter)++;
}

int counter = 0;

xgl_timer_config_t config = {
    .period_ms = 100,           /* 100ms period */
    .callback = timer_callback,
    .user_data = &counter,
    .auto_reload = true         /* Periodic timer */
};

/* Create timer */
xgl_timer_handle_t timer = xgl_timer_create(&config);

/* Start timer */
xgl_timer_start(timer);

/* Timer runs in background */
/* ... */

/* Stop timer */
xgl_timer_stop(timer);

/* Cleanup */
xgl_timer_destroy(timer);
```

### Time Wraparound Handling

All time functions handle 32-bit wraparound correctly:

```c
/* Time wraps around after ~49 days for milliseconds */
/* Time wraps around after ~71 minutes for microseconds */

/* This works correctly even across wraparound */
uint32_t start = 0xFFFFFFF0;  /* Near max value */
/* ... time passes and wraps to 0x00000010 ... */
uint32_t elapsed = xgl_time_elapsed_ms(start);  /* Returns 32 */
```

### Platform-Specific Notes

#### POSIX
- Uses `CLOCK_MONOTONIC` which is not affected by system time changes
- Nanosleep for accurate delays
- Microsecond precision available

#### Windows
- High-resolution performance counter
- Microsecond delays use busy-wait (Sleep has ~1ms resolution)
- Automatically initializes on first use

#### FreeRTOS
- Tick resolution depends on `configTICK_RATE_HZ`
- Microsecond functions have limited precision (1ms granularity)
- Hardware timers use static allocation for deterministic behavior

#### Bare-Metal
- Default implementation returns 0 (user must override)
- Use `xgl_time_set_source()` to provide custom time source
- Or override weak symbols in your application:

```c
/* In your application code */
uint32_t xgl_time_ms(void) {
    return read_hardware_timer_ms();
}

void xgl_delay_ms(uint32_t ms) {
    /* Your delay implementation */
}
```

### Performance

- **xgl_time_ms()**: ~50-200 CPU cycles depending on platform
- **xgl_time_elapsed_ms()**: ~50-200 CPU cycles (inline function)
- **xgl_time_is_timeout()**: ~50-200 CPU cycles (inline function)
- **xgl_delay_ms()**: Blocking, uses platform sleep or busy-wait

### Testing

Run time tests:

```bash
cmake -DXGL_BUILD_TESTS=ON -B build
cmake --build build --target xgl_tests
./build/test/xgl_tests --gtest_filter=XglTimeTest.*
```

### Requirements Validation

This implementation satisfies the following requirements:

- **Requirement 47.1**: Hardware timer callbacks for timeout management
- **Requirement 47.2**: Use hardware timers instead of polling when available
- **Requirement 47.3**: Abstraction for timer configuration
- **Requirement 47.4**: Support for multiple timer sources
- **Requirement 47.5**: Fallback to software timers when hardware unavailable

### API Reference

#### Time Functions

| Function | Description | Returns |
|----------|-------------|---------|
| `xgl_time_ms()` | Get current time in milliseconds | uint32_t |
| `xgl_time_us()` | Get current time in microseconds | uint32_t |
| `xgl_delay_ms(ms)` | Blocking delay in milliseconds | void |
| `xgl_delay_us(us)` | Blocking delay in microseconds | void |
| `xgl_time_elapsed_ms(start)` | Calculate elapsed time | uint32_t |
| `xgl_time_is_timeout(start, timeout)` | Check if timeout occurred | bool |
| `xgl_time_set_source(fn)` | Set custom time source | void |

#### Hardware Timer Functions

| Function | Description | Returns |
|----------|-------------|---------|
| `xgl_timer_create(config)` | Create hardware timer | xgl_timer_handle_t |
| `xgl_timer_start(handle)` | Start timer | xgl_error_t |
| `xgl_timer_stop(handle)` | Stop timer | xgl_error_t |
| `xgl_timer_destroy(handle)` | Destroy timer | void |

### Error Handling

Timer functions return `xgl_error_t`:

- `XGL_OK` - Success
- `XGL_ERR_NULL_POINTER` - NULL pointer passed
- `XGL_ERR_NOT_INITIALIZED` - Timer not supported on platform
- `XGL_ERR_BUSY` - Timer operation failed

## Adding New Platforms

To add support for a new platform:

1. Add platform detection in `xgl_mutex.h`:
   ```c
   #elif defined(__YOUR_PLATFORM__)
       #define XGL_PLATFORM_YOUR_PLATFORM
   ```

2. Implement mutex functions in `xgl_mutex.c`:
   ```c
   #elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_YOUR_PLATFORM)
   
   xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex) {
       /* Your implementation */
   }
   
   /* ... other functions ... */
   ```

3. Add tests in `test/test_mutex.cpp`

4. Update this README with platform details
