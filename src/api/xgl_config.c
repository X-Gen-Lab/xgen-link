/**
 * \file            xgl_config.c
 * \brief           Configuration management implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_network.h>
#include <xgl/xgl.h>

#include <string.h>
#include <xgen/memory/allocator.h>

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
#define XGL_MAX_FRAME_SIZE     2048  /**< Maximum frame size */

/*---------------------------------------------------------------------------*/
/* Configuration API                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get default configuration
 * \details         Fills configuration structure with sensible defaults
 *                  Uses medium preset as default (suitable for most
 * applications)
 */
void xgl_config_get_default(xgl_config_t* config) {
    /* Validate parameter */
    if (config == NULL) {
        return;
    }

    /* Select defaults that are supported by the compiled profile. */
#if !XGL_FEATURE_FRAGMENTATION
    xgl_config_get_preset_boot(config);
    return;
#else
    xgl_config_t default_config = XGL_CONFIG_PRESET_MEDIUM;

    /* Copy to output */
    memcpy(config, &default_config, sizeof(xgl_config_t));
#endif
}

/**
 * \brief           Configure the bounded single-peer boot transport
 * \param[out]      config: Destination configuration, or NULL
 */
void xgl_config_get_preset_boot(xgl_config_t* config) {
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
 * \details         Optimized for 32KB RAM, 50KB Flash
 *                  Minimal features, suitable for very constrained MCUs
 */
void xgl_config_get_preset_tiny(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_TINY;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/**
 * \brief           Get small configuration preset
 * \details         Optimized for 64KB RAM, 100KB Flash
 *                  Includes fragmentation support
 */
void xgl_config_get_preset_small(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_SMALL;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/**
 * \brief           Get medium configuration preset
 * \details         Optimized for 128KB RAM, 256KB Flash
 *                  Includes fragmentation. Compression is reserved and not
 *                  implemented in the current release.
 */
void xgl_config_get_preset_medium(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_MEDIUM;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/**
 * \brief           Get large configuration preset
 * \details         Optimized for 256KB+ RAM, 512KB+ Flash
 *                  Encryption is reserved and not implemented in the current
 * release.
 */
void xgl_config_get_preset_large(xgl_config_t* config) {
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
void xgl_config_get_preset_production(xgl_config_t* config) {
    if (config == NULL) {
        return;
    }

    xgl_config_t preset = XGL_CONFIG_PRESET_PRODUCTION;
    memcpy(config, &preset, sizeof(xgl_config_t));
}

/*---------------------------------------------------------------------------*/
/* Configuration Validation                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Validate configuration parameters
 * \details         Checks all configuration parameters for validity
 *                  Returns specific error codes for different validation
 * failures
 */
xgl_error_t xgl_config_validate(const xgl_config_t* config) {
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
    if (config->features.enable_fragmentation &&
        (config->features.max_message_size == 0U ||
         config->features.max_reassembly_slots == 0U ||
         config->features.max_reassembly_bytes == 0U ||
         config->features.max_tx_message_bytes == 0U ||
         config->features.max_message_size >
             config->features.max_reassembly_bytes)) {
        return XGL_ERR_INVALID_PARAM;
    }

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
        config->protocol.window_size > XGL_MAX_WINDOW_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (config->protocol.max_frame_size < XGL_MIN_FRAME_SIZE ||
        config->protocol.max_frame_size > XGL_MAX_FRAME_SIZE) {
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
#if !XGL_FEATURE_FRAGMENTATION
    if (config->features.enable_fragmentation) {
        return XGL_ERR_UNSUPPORTED;
    }
#endif
#if !XGL_FEATURE_OUT_OF_ORDER
    if (config->protocol.window_size != 1U ||
        config->features.max_peers != 1U || config->route_table_len > 1U) {
        return XGL_ERR_UNSUPPORTED;
    }
#endif

    if (config->auth_required) {
        if (config->auth_provider == NULL ||
            config->auth_provider->sign == NULL ||
            config->auth_provider->verify == NULL) {
            return XGL_ERR_INVALID_PARAM;
        }

        if (config->auth_provider->tag_len == 0U ||
            config->auth_provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
            return XGL_ERR_INVALID_PARAM;
        }
    }

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

        /* Check PHY operations */
        if (route->phy == NULL) {
            return XGL_ERR_INVALID_PARAM;
        }

        if (route->phy->tx == NULL || route->phy->rx == NULL) {
            return XGL_ERR_INVALID_PARAM;
        }

        /* Check max frame size */
        if (route->max_frame_size < XGL_MIN_FRAME_SIZE ||
            route->max_frame_size > XGL_MAX_FRAME_SIZE) {
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
