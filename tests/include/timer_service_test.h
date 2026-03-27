/**
 * @file tests/include/timer_service_test.h
 * @brief Timer service test – runs a simple test of the timer service.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This test runs on top of an already initialised system. It sends timer
 * commands and expects to see timer events in the log.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef TIMER_SERVICE_TEST_H
#define TIMER_SERVICE_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the timer service test.
 *
 * Assumes:
 * - command_router is initialised and locked
 * - event_dispatcher is started
 * - timer_service is registered and started
 *
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t timer_service_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* TIMER_SERVICE_TEST_H */