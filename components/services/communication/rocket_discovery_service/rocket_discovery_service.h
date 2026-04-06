/**
 * @file components/services/communication/rocket_discovery_service/rocket_discovery_service.h
 * @brief Rocket Discovery Service – publishes presence and capabilities.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service publishes a discovery message on `rocket/discovery` when the
 * system is ready (WiFi + MQTT connected). It also republishes periodically
 * so that late‑joining clients can discover the rocket.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026-04-06
 */

#ifndef ROCKET_DISCOVERY_SERVICE_H
#define ROCKET_DISCOVERY_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the rocket discovery service.
 *
 * @return ESP_OK on success.
 */
esp_err_t rocket_discovery_service_init(void);

/**
 * @brief Register any command handlers (none needed).
 *
 * @return ESP_OK.
 */
esp_err_t rocket_discovery_service_register_handlers(void);

/**
 * @brief Start the discovery service (subscribes to events, starts periodic timer).
 *
 * @return ESP_OK on success.
 */
esp_err_t rocket_discovery_service_start(void);

/**
 * @brief Stop the discovery service.
 *
 * @return ESP_OK.
 */
esp_err_t rocket_discovery_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ROCKET_DISCOVERY_SERVICE_H */