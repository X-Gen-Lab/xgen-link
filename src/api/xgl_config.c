/**
 * \file            xgl_config.c
 * \brief           Configuration management implementation
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <network/xgl_network.h>
#include <xgl/xgl_runtime.h>

#include "xgl_instance_internal.h"

#if XGL_FEATURE_AUTH
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>
#endif

/*---------------------------------------------------------------------------*/
/* Configuration Limits                                                      */
/*---------------------------------------------------------------------------*/

#define XGL_MIN_RX_BUFFER_SIZE 64    /**< Minimum RX buffer size */
#define XGL_MAX_RX_BUFFER_SIZE 4096  /**< Maximum RX buffer size */
#define XGL_MIN_ACK_TIMEOUT_MS 100   /**< Minimum ACK timeout */
#define XGL_MAX_ACK_TIMEOUT_MS 10000 /**< Maximum ACK timeout */
#define XGL_MIN_RETRY_COUNT    1     /**< Minimum retry count */
#define XGL_MAX_RETRY_COUNT    10    /**< Maximum retry count */
#define XGL_MIN_WINDOW_SIZE    1     /**< Minimum window size */
#define XGL_MAX_WINDOW_SIZE    32    /**< Maximum window size */
#define XGL_MIN_FRAME_SIZE     64    /**< Minimum frame size */
#define XGL_MAX_ROUTE_MTU      2048  /**< Maximum physical route MTU */

/*---------------------------------------------------------------------------*/
/* Configuration Validation                                                  */
/*---------------------------------------------------------------------------*/

#if XGL_FEATURE_AUTH
/**
 * \brief           Measure the largest mandatory authenticated base frame
 * \details         Use one ordinary ACK_RANGE and the validated fixed trailer
 *                  length to measure the required capacity.
 * \note            The base ACK exceeds HELLO and one-byte DATA overhead.
 *                  Optional SESSION and DATA_TYPE extensions are
 *                  measured separately for each actual transmission.
 */
static bool config_measure_required_auth_frame(size_t tag_len,
                                               size_t* frame_size) {
    uint8_t ack_value[32];
    size_t ack_value_len = 0U;
    const xgl_wire_ack_range_t range = {.gap = 0U, .length = 1U};
    if (xgl_wire_encode_ack_range_ext_value(ack_value, sizeof(ack_value), 0U,
                                            0U, &range, 1U,
                                            &ack_value_len) != XGL_OK) {
        return false;
    }

    uint8_t ack_ext[32];
    size_t ack_ext_len = 0U;
    if (xgl_wire_encode_ext(ack_ext, sizeof(ack_ext), XGL_WIRE_EXT_ACK_RANGE,
                            ack_value, ack_value_len, &ack_ext_len) != XGL_OK) {
        return false;
    }

    xgl_frame_layout_t layout;
    if (xgl_frame_measure_lengths(0U, ack_ext_len, tag_len, &layout) !=
        XGL_OK) {
        return false;
    }
    *frame_size = layout.frame_len;
    return true;
}
#endif

/** \brief           Resolve the optional policy before walking any routes. */
static xgl_error_t config_validate_options(const xgl_config_t* config,
                                           bool* auth_required) {
    *auth_required = config->auth_required;
    const xgl_instance_options_t* options = config->options;
    if (options == NULL) {
        return XGL_OK;
    }
    if (options->size != sizeof(*options) ||
        options->version != XGL_INSTANCE_OPTIONS_VERSION ||
        options->config_size != sizeof(*config)) {
        return XGL_ERR_INVALID_VERSION;
    }
#if XGL_FEATURE_ADVANCED_RUNTIME
    if (options->tx_lifetime_ms >= UINT32_C(0x80000000)) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (options->protect_installed_scopes) {
#if XGL_FEATURE_AUTH
        if (config->auth_provider == NULL) {
            return XGL_ERR_INVALID_PARAM;
        }
        *auth_required = true;
#else
        return XGL_ERR_UNSUPPORTED;
#endif
    }
    return XGL_OK;
#else
    return XGL_ERR_UNSUPPORTED;
#endif
}

/** \brief           Validate the configuration used to plan the workspace. */
xgl_error_t xgl_instance_validate_config(const xgl_config_t* config) {
    if (config == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    bool auth_required;
    const xgl_error_t option_error =
        config_validate_options(config, &auth_required);
    if (option_error != XGL_OK) {
        return option_error;
    }

    /* Validate memory configuration */
    if (config->source_id == 0 || config->source_id == XGL_BROADCAST_ID) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (config->memory.rx_buffer_size < XGL_MIN_RX_BUFFER_SIZE ||
        config->memory.rx_buffer_size > XGL_MAX_RX_BUFFER_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (config->features.max_peers == 0U ||
        config->features.max_tx_packets == 0U ||
        config->features.peer_idle_timeout_ms >= UINT32_C(0x80000000)) {
        return XGL_ERR_INVALID_PARAM;
    }
#if XGL_FEATURE_OUT_OF_ORDER
    if (config->protocol.window_size > 1U &&
        config->features.max_rx_buffered_packets == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif
    /* Validate protocol parameters */
    if (config->protocol.ack_timeout_ms < XGL_MIN_ACK_TIMEOUT_MS ||
        config->protocol.ack_timeout_ms > XGL_MAX_ACK_TIMEOUT_MS) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (config->protocol.max_retry_count < XGL_MIN_RETRY_COUNT ||
        config->protocol.max_retry_count > XGL_MAX_RETRY_COUNT) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (config->protocol.window_size < XGL_MIN_WINDOW_SIZE ||
        config->protocol.window_size > XGL_MAX_WINDOW_SIZE ||
        config->protocol.window_size > XGL_BUILD_WINDOW_CAPACITY) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (config->protocol.max_frame_size < XGL_MIN_FRAME_SIZE ||
        config->protocol.max_frame_size > XGL_DATALINK_MAX_FRAME_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Validate frame size is larger than header */
    if (config->protocol.max_frame_size <
        XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }

#if !XGL_FEATURE_AUTH
    if (auth_required) {
        return XGL_ERR_UNSUPPORTED;
    }
#endif
#if !XGL_FEATURE_OUT_OF_ORDER
    if (config->protocol.window_size != 1U ||
        config->features.max_peers != 1U || config->route_table_len > 1U) {
        return XGL_ERR_UNSUPPORTED;
    }
#endif

#if XGL_FEATURE_AUTH
    size_t required_auth_frame_size = 0U;
    if ((auth_required && config->auth_provider == NULL) ||
        (config->auth_provider != NULL &&
         (config->auth_provider->sign == NULL ||
          config->auth_provider->verify == NULL ||
          config->auth_provider->tag_len == 0U ||
          config->auth_provider->tag_len > XGL_AUTH_TAG_MAX_LEN))) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (auth_required &&
        (!config_measure_required_auth_frame(config->auth_provider->tag_len,
                                             &required_auth_frame_size) ||
         config->protocol.max_frame_size < required_auth_frame_size)) {
        return XGL_ERR_INVALID_PARAM;
    }
#endif

    /* Validate RX buffer is large enough for the configured full frame.
     * max_frame_size includes header, payload, and CRC16.
     */
    size_t required_rx_buffer_size = (size_t)config->protocol.max_frame_size;
    if (config->memory.rx_buffer_size < required_rx_buffer_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    /* Validate routing configuration */
    if (config->route_table_len > UINT16_MAX ||
        (config->route_table_len > 0 && config->route_table == NULL)) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Validate route table entries */
    for (size_t i = 0; i < config->route_table_len; i++) {
        const xgl_route_item_t* route = &config->route_table[i];

        if (route->target_id == 0U || route->target_id == config->source_id) {
            return XGL_ERR_INVALID_PARAM;
        }
#if XGL_FEATURE_OUT_OF_ORDER
        /* Restricted profiles already rejected more than one route above. */
        for (size_t j = 0U; j < i; ++j) {
            if (config->route_table[j].target_id == route->target_id) {
                return XGL_ERR_INVALID_PARAM;
            }
        }
#endif

        /* Check PHY operations */
        if (route->phy == NULL) {
            return XGL_ERR_INVALID_PARAM;
        }

        if (route->phy->tx == NULL || route->phy->rx == NULL) {
            return XGL_ERR_INVALID_PARAM;
        }

        /* Check max frame size */
        if (route->max_frame_size < XGL_MIN_FRAME_SIZE ||
            route->max_frame_size > XGL_MAX_ROUTE_MTU) {
            return XGL_ERR_INVALID_PARAM;
        }

        /* Check read frequency */
        if (route->read_freq_hz == 0) {
            return XGL_ERR_INVALID_PARAM;
        }
    }

    /* All validation passed */
    return XGL_OK;
}
