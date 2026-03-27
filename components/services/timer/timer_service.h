/**
 * @file components/services/timer/timer_service.h
 * @brief Timer Service – manages FreeRTOS software timers.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns software timers that are used by the system to generate
 * timed events. It responds to start/stop commands and posts timeout events
 * to the core via the event dispatcher.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - COMMAND_START_TIMER       : start a one-shot timer (params: timeout_ms)
 * - COMMAND_STOP_TIMER        : stop the one-shot timer
 * - COMMAND_START_PERIODIC_TIMER : start a periodic timer (params: interval_ms)
 * - COMMAND_STOP_PERIODIC_TIMER  : stop the periodic timer
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_TIMER_EXPIRED       : when a one-shot timer expires
 * - EVENT_PERIODIC_TIMER_EXPIRED : when a periodic timer expires
 *
 * Note: EVENT_TIMER_EXPIRED and EVENT_PERIODIC_TIMER_EXPIRED must be defined
 * in event_types.h. We'll add them now.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef TIMER_SERVICE_H
#define TIMER_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the timer service.
 *
 * Creates the FreeRTOS software timers.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t timer_service_init(void);

/**
 * @brief Register timer service command handlers.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t timer_service_register_handlers(void);

/**
 * @brief Start the timer service.
 *
 * Does nothing (timers are managed by commands).
 *
 * @return ESP_OK.
 */
esp_err_t timer_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TIMER_SERVICE_H */