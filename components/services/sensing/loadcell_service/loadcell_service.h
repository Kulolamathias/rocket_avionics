/**
 * @file loadcell_service.h
 * @brief Load Cell Service – high‑frequency thrust measurement.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the load cell driver and provides a command interface
 * to start/stop continuous sampling, calibrate, and retrieve readings.
 * It runs a dedicated task that reads the sensor at a configurable rate
 * and publishes the data via MQTT.
 *
 * =============================================================================
 * COMMANDS HANDLED
 * =============================================================================
 * - CMD_LOADCELL_START_SAMPLING  – begin continuous reading and publishing
 * - CMD_LOADCELL_STOP_SAMPLING   – stop sampling
 * - CMD_LOADCELL_CALIBRATE_ZERO  – zero calibration
 * - CMD_LOADCELL_CALIBRATE_SCALE – scale calibration with known weight
 * - CMD_LOADCELL_GET_LAST        – get the latest reading
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_LOADCELL_DATA          – published when a new sample is taken
 *
 * =============================================================================
 * CONFIGURATION MACROS (can be moved to Kconfig)
 * =============================================================================
 * - LOADCELL_SAMPLE_RATE_HZ      – sensor read frequency (max 80)
 * - LOADCELL_PUBLISH_RATE_HZ     – MQTT publish frequency (1‑80)
 * - LOADCELL_FILTER_WINDOW       – moving average window size
 * - LOADCELL_MQTT_TOPIC          – base topic for data
 * =============================================================================
 */

#ifndef LOADCELL_SERVICE_H
#define LOADCELL_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the load cell service.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t loadcell_service_init(void);

/**
 * @brief Register command handlers with the command router.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t loadcell_service_register_handlers(void);

/**
 * @brief Start the load cell service (begins sampling task).
 * @return ESP_OK on success.
 */
esp_err_t loadcell_service_start(void);

/**
 * @brief Stop the load cell service (stops sampling task).
 * @return ESP_OK on success.
 */
esp_err_t loadcell_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* LOADCELL_SERVICE_H */