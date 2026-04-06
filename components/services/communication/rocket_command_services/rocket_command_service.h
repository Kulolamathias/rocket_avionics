/**
 * @file components/services/communication/rocket_command_service/rocket_command_service.h
 * @brief Rocket Command Service – handles MQTT commands from web dashboard.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service subscribes to rocket/<mac>/cmd/+ MQTT topics, parses incoming
 * JSON commands, and executes the corresponding rocket commands via the
 * command router. It does NOT post events; it only translates MQTT messages
 * into system commands.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - ignition      -> COMMAND_IGNITION
 * - parachute     -> COMMAND_DEPLOY_PARACHUTE
 * - explode       -> COMMAND_EXPLODE
 * - config        -> COMMAND_UPDATE_CONFIG
 * - calibrate     -> COMMAND_CALIBRATE_SENSOR
 * - request_data  -> COMMAND_SET_TELEMETRY_RATE
 *
 * =============================================================================
 * DEPENDENCIES
 * =============================================================================
 * - mqtt_service (for subscription and message reception)
 * - command_router (to execute commands)
 * - cJSON (for JSON parsing)
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-05
 */

#ifndef ROCKET_COMMAND_SERVICE_H
#define ROCKET_COMMAND_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the rocket command service.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rocket_command_service_init(void);

/**
 * @brief Register command handlers with the command router.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rocket_command_service_register_handlers(void);

/**
 * @brief Start the rocket command service.
 *
 * Subscribes to MQTT command topics.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t rocket_command_service_start(void);

/**
 * @brief Stop the rocket command service.
 *
 * @return ESP_OK on success.
 */
esp_err_t rocket_command_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ROCKET_COMMAND_SERVICE_H */