/**
 * @file components/services/actuation/ignition_relay_service/ignition_relay_service.h
 * @brief Ignition Relay Service – controls engine ignition via a relay.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service handles the COMMAND_IGNITION command by setting a GPIO pin
 * high (or low) to activate a relay that starts the engine. It also posts
 * EVENT_ENGINE_IGNITED to notify the core that ignition has occurred.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - COMMAND_IGNITION  – enable/disable ignition relay (params: ignition_params_t)
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_ENGINE_IGNITED – when ignition is enabled
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-06
 */

#ifndef IGNITION_RELAY_SERVICE_H
#define IGNITION_RELAY_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Initialise the ignition relay service.
 *
 * Configures the GPIO pin for the relay (default: output, low).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ignition_relay_service_init(void);

/**
 * @brief Register command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t ignition_relay_service_register_handlers(void);

/**
 * @brief Start the ignition relay service (no background tasks).
 *
 * @return ESP_OK always.
 */
esp_err_t ignition_relay_service_start(void);

/**
 * @brief Stop the ignition relay service (turn off relay).
 *
 * @return ESP_OK always.
 */
esp_err_t ignition_relay_service_stop(void);


// void test_relay_manually(void);

#ifdef __cplusplus
}
#endif

#endif /* IGNITION_RELAY_SERVICE_H */