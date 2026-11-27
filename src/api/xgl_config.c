/**
 * \file            xgl_config.c
 * \brief           Configuration management implementation
 * \author          X-Gen Lab
 */

#include <network/xgl_network.h>
#include <xgl/xgl.h>

#if XGL_FEATURE_AUTH
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>
#endif

#include <string.h>

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
/* Configuration API                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get default configuration
 * \details         Uses the profile default with the compiled frame limit.
 */
void xgl_config_get_default_v5(xgl_config_t* config) {
    /* Validate parameter */
    if (config == NULL) {
        return;
    }

    /* Select defaults that are supported by the compiled profile. */
#if XGL_BUILD_PROFILE_BOOT
    xgl_config_get_preset_boot_v5(config);
#else
    xgl_config_t default_config = XGL_CONFIG_PRESET_MEDIUM;
    if (default_config.protocol.window_size > XGL_BUILD_WINDOW_CAPACITY) {
        default_config.protocol.window_size = XGL_BUILD_WINDOW_CAPACITY;
    }

    /* Copy to output */
    memcpy(config, &default_config, sizeof(xgl_config_t));
#endif
    if (config->protocol.max_frame_size > XGL_DATALINK_MAX_FRAME_SIZE) {
        config->protocol.max_frame_size = XGL_DATALINK_MAX_FRAME_SIZE;
    }
}

/**
 * \brief           Configure the bounded single-peer boot transport
 * \param[out]      config: Destination configuration, or NULL
 */
void xgl_config_get_preset_boot_v5(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }
    xgl_config_t preset = XGL_CONFIG_PRESET_TINY;
    preset.name = "boot";
    preset.protocol.window_size = 1U;
    preset.features.max_peers = 1U;
    preset.features.max_tx_packets = 1U;
    preset.features.max_rx_buffered_packets = 0U;
    preset.memory.rx_buffer_size = 128U;
    memcpy(config, &preset, sizeof(*config));
}

/**
 * \brief           Get tiny configuration preset
 * \details         Query exact workspace bytes with xgl_memory_report().
 */
void xgl_config_get_preset_tiny_v5(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_TINY;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/**
 * \brief           Get small configuration preset
 * \details         Query workspace bytes before selecting a device memory
 *                  budget.
 */
void xgl_config_get_preset_small_v5(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_SMALL;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/**
 * \brief           Get medium configuration preset
 * \details         Increased bounded packet and peer capacities.
 */
void xgl_config_get_preset_medium_v5(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_MEDIUM;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/**
 * \brief           Get large configuration preset
 * \details         Largest predefined bounded capacities; not a device-size
 *                  guarantee. Encryption is not implemented in the current
 * release.
 */
void xgl_config_get_preset_large_v5(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_LARGE;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/**
 * \brief           Get production configuration preset
 * \details         Production profile requires authentication provider wiring.
 */
void xgl_config_get_preset_production_v5(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_PRODUCTION;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/*---------------------------------------------------------------------------*/
/* Configuration Validation                                                  */
/*---------------------------------------------------------------------------*/

#if XGL_FEATURE_AUTH
/**
 * \brief           Measure the largest mandatory authenticated base frame
 * \param[in]       tag_len: Validated fixed authentication trailer size
 * \param[out]      frame_size: Required bytes for one ordinary ACK_RANGE
 * \return          True when the wire encoder accepts the base frame
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

/**
 * \brief           Validate configuration parameters
 * \details         Checks all configuration parameters for validity
 *                  Returns specific error codes for different validation
 * failures
 */
xgl_error_t xgl_config_validate_v5(const xgl_config_t* config) {
    /* Check for NULL pointer */
    if (config == NULL) {
        return XGL_ERR_NULL_POINTER;
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

    if (config->features.enable_compression ||
        config->features.enable_encryption) {
        return XGL_ERR_INVALID_PARAM;
    }

#if !XGL_FEATURE_AUTH
    if (config->auth_required) {
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
    if ((config->auth_required && config->auth_provider == NULL) ||
        (config->auth_provider != NULL &&
         (config->auth_provider->sign == NULL ||
          config->auth_provider->verify == NULL ||
          config->auth_provider->tag_len == 0U ||
          config->auth_provider->tag_len > XGL_AUTH_TAG_MAX_LEN))) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (config->auth_required &&
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
