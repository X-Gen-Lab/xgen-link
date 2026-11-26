/**
 * \file            public_api_smoke.c
 * \brief           Installed C11 creation, deadlines and optional runtime APIs
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

#include <stdalign.h>

/** \brief           Explicit storage and observations owned by this consumer.
 */
typedef struct {
    alignas(xgl_max_align_t) uint8_t storage[16384];
    size_t allocations;
    size_t releases;
    size_t reads;
    bool fail;
    bool allocated;
    bool invalid_release;
} smoke_state_t;

/** \brief           Reserve the complete protocol workspace exactly once. */
static void* smoke_allocate(void* context, size_t size) {
    smoke_state_t* state = context;
    ++state->allocations;
    if (state->fail || state->allocated || size > sizeof(state->storage)) {
        return NULL;
    }
    state->allocated = true;
    return state->storage;
}

/** \brief           Verify that destruction returns the original workspace. */
static void smoke_release(void* context, void* memory) {
    smoke_state_t* state = context;
    if (memory == NULL) {
        return;
    }
    if (!state->allocated || memory != state->storage) {
        state->invalid_release = true;
        return;
    }
    state->allocated = false;
    ++state->releases;
}

/** \brief           Supply a synchronous output without retaining bytes. */
static xgl_error_t smoke_transmit(const uint8_t* data, size_t length,
                                  void* context) {
    (void)data;
    (void)length;
    (void)context;
    return XGL_OK;
}

/** \brief           Observe actual polling independently of deadline queries.
 */
static xgl_error_t smoke_receive(uint8_t* data, size_t* length, void* context) {
    smoke_state_t* state = context;
    (void)data;
    ++state->reads;
    *length = 0U;
    return XGL_OK;
}

/** \brief           Check optional APIs through the installed capability ID. */
static int smoke_optional_runtime(const xgl_config_t* config,
                                  smoke_state_t* state) {
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    xgl_handle_t handle = NULL;
    options.build_config_id ^= 1U;
    if (xgl_create_with_options(config, &options, &handle) !=
            XGL_ERR_INVALID_VERSION ||
        handle != NULL || state->allocations != 2U) {
        return 12;
    }
    options.build_config_id ^= 1U;
    xgl_memory_report_t report = {0};
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (xgl_memory_report_with_options(config, &options, &report) != XGL_OK ||
        report.requirements.size == 0U ||
        report.requirements.size > sizeof(state->storage)) {
        return 13;
    }
    if (xgl_create_with_options(config, &options, &handle) != XGL_OK ||
        handle == NULL || state->allocations != 3U ||
        xgl_init(handle) != XGL_OK) {
        return 14;
    }
    xgl_resource_snapshot_t snapshot = {0};
    xgl_execution_result_t result = {true};
    const xgl_execution_limits_t limits = {0U, 0U};
    const xgl_work_budget_t disabled = {0U, 100U};
    if (xgl_resources_get(handle, &snapshot) != XGL_OK ||
        snapshot.peer_count != 0U || snapshot.tx_packets != 0U ||
        snapshot.reassembly_slots != 0U ||
        xgl_step_bounded(handle, 20U, &disabled, &limits, &result) != XGL_OK ||
        result.pending || state->reads != 1U) {
        return 15;
    }
    xgl_destroy(handle);
    if (state->allocated || state->invalid_release || state->releases != 2U) {
        return 16;
    }
    handle = NULL;
    if (xgl_init_static_with_options(config, &options, state->storage,
                                     report.requirements.size,
                                     &handle) != XGL_OK ||
        handle == NULL || state->allocations != 3U) {
        return 17;
    }
    const xgl_work_budget_t enabled = {128U, 100U};
    if (xgl_step_bounded(handle, 30U, &enabled, &limits, &result) != XGL_OK ||
        result.pending || state->reads != 2U) {
        return 18;
    }
    xgl_destroy(handle);
    return state->allocated || state->invalid_release || state->releases != 2U
               ? 19
               : 0;
#else
    return xgl_memory_report_with_options(config, &options, &report) !=
                       XGL_ERR_UNSUPPORTED ||
                   xgl_create_with_options(config, &options, &handle) !=
                       XGL_ERR_UNSUPPORTED ||
                   handle != NULL || state->allocations != 2U
               ? 20
               : 0;
#endif
}

/** \brief           Exercise only public installed headers and exported APIs.
 */
int main(void) {
    static smoke_state_t state;
    const xgl_allocator_t allocator = {
        .alloc = smoke_allocate, .free = smoke_release, .ctx = &state};
    xgl_phy_ops_t phy = {smoke_transmit, smoke_receive, &state};
    xgl_route_item_t route = {2U, &phy, 128U, 100U, 0U};
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    config.memory.allocator = &allocator;
    config.route_table = &route;
    config.route_table_len = 1U;

    xgl_handle_t handle = NULL;
    if (xgl_create_with_error_checked(
            &config, sizeof(config) - 1U, XGL_CONFIG_ABI_VERSION,
            XGL_BUILD_CONFIG_ID, &handle) != XGL_ERR_INVALID_VERSION ||
        handle != NULL || state.allocations != 0U) {
        return 1;
    }
    if (xgl_create_with_error_checked(
            &config, sizeof(config), XGL_CONFIG_ABI_VERSION ^ 1U,
            XGL_BUILD_CONFIG_ID, &handle) != XGL_ERR_INVALID_VERSION ||
        handle != NULL || state.allocations != 0U) {
        return 2;
    }
    if (xgl_create_with_error_checked(
            &config, sizeof(config), XGL_CONFIG_ABI_VERSION,
            XGL_BUILD_CONFIG_ID ^ 1U, &handle) != XGL_ERR_INVALID_VERSION ||
        handle != NULL || state.allocations != 0U) {
        return 3;
    }

    state.fail = true;
    if (xgl_create_with_error(&config, &handle) != XGL_ERR_NO_MEMORY ||
        handle != NULL || state.allocations != 1U || state.allocated) {
        return 4;
    }
    state.fail = false;
    if (xgl_create_with_error(&config, &handle) != XGL_OK || handle == NULL ||
        state.allocations != 2U || !state.allocated) {
        return 5;
    }

    const xgl_work_budget_t enabled = {128U, 100U};
    const xgl_work_budget_t disabled = {0U, 100U};
    uint32_t delay = 77U;
    if (xgl_next_timeout_for_work(handle, 0U, &enabled, &delay) ||
        delay != 77U || xgl_init(handle) != XGL_OK) {
        return 6;
    }
    if (xgl_next_timeout_for_work(handle, 0U, &disabled, &delay) ||
        delay != 77U || state.reads != 0U) {
        return 7;
    }
    if (!xgl_next_timeout_for_work(handle, 0U, &enabled, &delay) ||
        delay != 0U || state.reads != 0U ||
        xgl_step(handle, 10U, &enabled) != XGL_OK || state.reads != 1U) {
        return 8;
    }
    if (!xgl_next_timeout_for_work(handle, 12U, &enabled, &delay) ||
        delay != 8U || state.reads != 1U) {
        return 9;
    }
    delay = 77U;
    if (xgl_next_timeout_for_work(handle, 12U, &disabled, &delay) ||
        delay != 77U || state.reads != 1U) {
        return 10;
    }

    xgl_destroy(handle);
    if (state.allocated || state.invalid_release || state.allocations != 2U ||
        state.releases != 1U) {
        return 11;
    }
    return smoke_optional_runtime(&config, &state);
}
