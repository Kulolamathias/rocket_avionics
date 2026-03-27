/**
 * @file tests/include/core_self_test.h
 * @brief Core self‑test – validates deterministic behavior of core layer.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This test is intended to run in isolation (without real services). It
 * registers mock command handlers and simulates a full flight profile.
 * It cannot run alongside real services because it would conflict with
 * their command handlers.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#ifndef CORE_SELF_TEST_H
#define CORE_SELF_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the core self‑test scenario.
 *
 * This test assumes that no services have been initialised and that
 * the command router is not yet locked. It will initialise core modules
 * itself and run the simulation.
 *
 * @return ESP_OK on success.
 */
esp_err_t core_self_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_SELF_TEST_H */