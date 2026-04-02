/**
 * @file components/services/sensing/mpu_service/mpu_service.h
 * @brief MPU Service – periodic sensor reads and event posting.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This service owns the MPU driver and performs periodic sensor updates.
 * It posts events (raw accelerometer, gyroscope, orientation) to the core
 * at a configurable rate. It receives no commands; it runs automatically.
 *
 * =============================================================================
 * EVENTS POSTED
 * =============================================================================
 * - EVENT_ACCELERATION_UPDATE   – linear acceleration (ax, ay, az)
 * - EVENT_GYRO_UPDATE           – angular velocity (gx, gy, gz)
 * - EVENT_ORIENTATION_UPDATE    – roll, pitch, yaw + quaternion
 *
 * =============================================================================
 * DESIGN NOTES
 * =============================================================================
 * - The service runs a dedicated FreeRTOS task that updates at the configured
 *   sample rate (default 200 Hz). No commands are handled.
 * - Calibration is performed once at startup (optional).
 * =============================================================================
 */

#ifndef MPU_SERVICE_H
#define MPU_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the MPU service (creates driver, starts task).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mpu_service_init(void);

/**
 * @brief Register MPU service command handlers (none currently).
 * @return ESP_OK always.
 */
esp_err_t mpu_service_register_handlers(void);

/**
 * @brief Start the MPU service (already started by init).
 * @return ESP_OK always.
 */
esp_err_t mpu_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MPU_SERVICE_H */