/**
 * @file components/services/logging/logging_service.h
 * @brief Logging Service – central logging facility.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * Provides a command‑based logging interface. The core (state_manager) issues
 * COMMAND_LOG_MESSAGE when an event needs to be logged. This service prints
 * the message with the appropriate ESP_LOG level.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef LOGGING_SERVICE_H
#define LOGGING_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the logging service.
 * @return ESP_OK.
 */
esp_err_t logging_service_init(void);

/**
 * @brief Register command handlers.
 * @return ESP_OK on success.
 */
esp_err_t logging_service_register_handlers(void);

/**
 * @brief Start the logging service.
 * @return ESP_OK.
 */
esp_err_t logging_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_SERVICE_H */