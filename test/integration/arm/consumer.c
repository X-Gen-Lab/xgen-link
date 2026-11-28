/**
 * \file            consumer.c
 * \brief           Exercise an installed Boot SDK in a Cortex-M0 emulator
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-11-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <xgl/xgl.h>

enum {
    SYS_WRITE0 = 0x04U,
    SYS_EXIT_EXTENDED = 0x20U,
    ADP_STOPPED_APPLICATION_EXIT = 0x20026U
};

extern uint8_t __data_load[];
extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];
extern uint8_t __stack_top[];

_Alignas(xgl_max_align_t) static uint8_t
    s_workspace[XGL_REFERENCE_WORKSPACE_BYTES];
static unsigned s_tx_count;

/** \brief Pass an operation to the emulator's semihosting service. */
static void semihost(uint32_t operation, const void* argument) {
    register uint32_t r0 __asm__("r0") = operation;
    register const void* r1 __asm__("r1") = argument;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
}

/** \brief Report the actual target size without pulling stdio into the image.
 */
static void write_size(const char* label, size_t value) {
    char digits[11];
    size_t index = sizeof(digits) - 1U;
    digits[index] = '\0';
    do {
        digits[--index] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    semihost(SYS_WRITE0, label);
    semihost(SYS_WRITE0, &digits[index]);
    semihost(SYS_WRITE0, "\n");
}

/** \brief Count accepted frames at the synchronous PHY boundary. */
static xgl_error_t send_frame(const uint8_t* data, size_t length,
                              void* user_data) {
    (void)data;
    (void)length;
    (void)user_data;
    ++s_tx_count;
    return XGL_OK;
}

/** \brief Model a nonblocking PHY with no incoming bytes. */
static xgl_error_t read_frame(uint8_t* data, size_t* length, void* user_data) {
    (void)data;
    (void)user_data;
    *length = 0U;
    return XGL_OK;
}

/** \brief Measure, initialize, send, service, and destroy the real instance. */
static bool run_reference(void) {
    static xgl_phy_ops_t phy = {.tx = send_frame, .rx = read_frame};
    static xgl_route_item_t route = {.target_id = 2U,
                                     .phy = &phy,
                                     .max_frame_size = 128U,
                                     .read_freq_hz = 100U};
    static const xgl_config_t config = {
        .name = "arm-reference",
        .source_id = 1U,
        .memory = {.rx_buffer_size = 128U},
        .protocol = {.ack_timeout_ms = 1000U,
                     .max_retry_count = 3U,
                     .window_size = 1U,
                     .max_frame_size = 128U},
        .features = {.max_tx_packets = 1U,
                     .peer_idle_timeout_ms = 60000U,
                     .max_peers = 1U},
        .route_table = &route,
        .route_table_len = 1U};
    xgl_memory_requirements_t requirements = {0};
    if (xgl_memory_requirements(&config, &requirements) != XGL_OK) {
        return false;
    }
    write_size("workspace_required=", requirements.size);
    write_size("workspace_alignment=", requirements.alignment);
    write_size("workspace_reserved=", sizeof(s_workspace));
    write_size("resource_config_id=", XGL_RESOURCE_CONFIG_ID);
    write_size("adaptive_rto=", XGL_FEATURE_ADAPTIVE_RTO);
    write_size("window_capacity=", XGL_BUILD_WINDOW_CAPACITY);

    xgl_handle_t handle = NULL;
    const xgl_error_t initialized =
        xgl_init_static(&config, s_workspace, sizeof(s_workspace), &handle);
    write_size("initialized=", (size_t)initialized);
    if (initialized != XGL_OK) {
        return false;
    }
    static const uint8_t payload[] = {0x12U, 0x34U};
    const xgl_tx_data_t request = {.target_id = 2U,
                                   .data = payload,
                                   .data_len = sizeof(payload),
                                   .reliable = true};
    const xgl_error_t sent = xgl_send_at(handle, &request, 0U);
    write_size("sent=", (size_t)sent);
    const unsigned initial_frames = s_tx_count;
    const xgl_work_budget_t budget = {0};
    uint32_t retry_ms = 0U;
    bool passed = false;
    if (sent == XGL_OK &&
        xgl_next_timeout_for_work(handle, 0U, &budget, &retry_ms)) {
        const xgl_error_t stepped = xgl_step(handle, retry_ms, &budget);
        write_size("retry_ms=", retry_ms);
        write_size("stepped=", (size_t)stepped);
        write_size("frames_before_retry=", initial_frames);
        write_size("frames_after_retry=", s_tx_count);
        passed = stepped == XGL_OK && initial_frames > 0U &&
                 s_tx_count == initial_frames + 1U;
    }
    xgl_destroy(handle);
    return passed;
}

/** \brief Initialize generic linker sections and report the emulator result. */
void Reset_Handler(void) {
    const uint8_t* source = __data_load;
    for (uint8_t* target = __data_start;
         (uintptr_t)target < (uintptr_t)__data_end; ++target) {
        *target = *source++;
    }
    for (uint8_t* target = __bss_start;
         (uintptr_t)target < (uintptr_t)__bss_end; ++target) {
        *target = 0U;
    }
    const bool passed = run_reference();
    semihost(SYS_WRITE0, passed ? "xgl-arm-reference: pass\n"
                                : "xgl-arm-reference: fail\n");
    const uint32_t status[] = {ADP_STOPPED_APPLICATION_EXIT, passed ? 0U : 1U};
    semihost(SYS_EXIT_EXTENDED, status);
    for (;;) {
        /* The semihosting service normally exits the emulator. */
    }
}

__attribute__((section(".isr_vector"), used)) static const struct {
    const void* stack;
    void (*reset)(void);
} s_vectors = {__stack_top, Reset_Handler};
