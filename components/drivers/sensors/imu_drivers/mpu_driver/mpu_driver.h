/**
 * @file components/drivers/sensors/imu/mpu_driver/mpu_driver.h
 * @brief MPU-9250/6500/9255 Driver – I2C communication and sensor fusion.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to the MPU series IMU.
 * It handles I2C communication, raw sensor reads, calibration, and basic
 * orientation computation (quaternion). It contains NO business logic,
 * NO command handling, and NO event posting.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 * - Deterministic: fixed read timing, no blocking delays.
 * - Configurable sample rate (up to 1 kHz for gyro, 1 kHz for accel).
 * - Calibration offsets can be stored and applied.
 * - Orientation computed using a complementary filter (fast, low CPU).
 * - Supports accelerometer, gyroscope, and magnetometer (MPU‑9250).
 * =============================================================================
 * 
 * @author matthithyahu
 * @date 2026
 * 
 */

#ifndef MPU_DRIVER_H
#define MPU_DRIVER_H

#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle for an MPU instance. */
typedef struct mpu_handle_t *mpu_handle_t;

/**
 * @brief Raw sensor data from MPU.
 */
typedef struct {
    float accel_x;      /**< Acceleration X (m/s²) */
    float accel_y;      /**< Acceleration Y (m/s²) */
    float accel_z;      /**< Acceleration Z (m/s²) */
    float gyro_x;       /**< Angular velocity X (rad/s) */
    float gyro_y;       /**< Angular velocity Y (rad/s) */
    float gyro_z;       /**< Angular velocity Z (rad/s) */
    float mag_x;        /**< Magnetic field X (µT) – only for MPU‑9250 */
    float mag_y;        /**< Magnetic field Y (µT) */
    float mag_z;        /**< Magnetic field Z (µT) */
    bool accel_valid;
    bool gyro_valid;
    bool mag_valid;
} mpu_raw_data_t;

/**
 * @brief Orientation data (Euler angles and quaternion).
 */
typedef struct {
    float roll_rad;     /**< Roll angle (radians) */
    float pitch_rad;    /**< Pitch angle (radians) */
    float yaw_rad;      /**< Yaw angle (radians) – requires magnetometer */
    float q_w;          /**< Quaternion w component */
    float q_x;          /**< Quaternion x component */
    float q_y;          /**< Quaternion y component */
    float q_z;          /**< Quaternion z component */
    bool orientation_valid;
} mpu_orientation_t;

/**
 * @brief Calibration offsets for accelerometer and gyroscope.
 */
typedef struct {
    float accel_offset_x;
    float accel_offset_y;
    float accel_offset_z;
    float gyro_offset_x;
    float gyro_offset_y;
    float gyro_offset_z;
    float mag_offset_x;
    float mag_offset_y;
    float mag_offset_z;
} mpu_calibration_t;

/**
 * @brief MPU configuration.
 */
typedef struct {
    i2c_port_t i2c_port;           /**< I2C port number (I2C_NUM_0 or I2C_NUM_1) */
    uint8_t i2c_addr;              /**< I2C address (default 0x68 for MPU‑9250) */
    uint8_t scl_pin;               /**< SCL GPIO pin */
    uint8_t sda_pin;               /**< SDA GPIO pin */
    uint32_t sample_rate_hz;       /**< Desired sample rate (Hz) – max 1000 for gyro, 4000 for accel */
    bool enable_magnetometer;      /**< Enable magnetometer (MPU‑9250 only) */
    mpu_calibration_t calibration; /**< Initial calibration offsets (zero if unknown) */
} mpu_config_t;

/**
 * @brief Create an MPU instance.
 *
 * @param cfg   Configuration structure.
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mpu_driver_create(const mpu_config_t *cfg, mpu_handle_t *out_handle);

/**
 * @brief Perform a full sensor read (non‑blocking).
 *
 * Reads accelerometer, gyroscope, and optionally magnetometer.
 * Applies calibration and updates internal data.
 *
 * @param handle MPU instance handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mpu_driver_update(mpu_handle_t handle);

/**
 * @brief Get the latest raw sensor data.
 *
 * @param handle MPU instance handle.
 * @param[out] data Pointer to store raw data.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mpu_driver_get_raw(mpu_handle_t handle, mpu_raw_data_t *data);

/**
 * @brief Get the latest orientation (Euler + quaternion).
 *
 * @param handle MPU instance handle.
 * @param[out] orientation Pointer to store orientation.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mpu_driver_get_orientation(mpu_handle_t handle, mpu_orientation_t *orientation);

/**
 * @brief Set calibration offsets (e.g., after a calibration routine).
 *
 * @param handle MPU instance handle.
 * @param calib Pointer to calibration offsets.
 * @return ESP_OK on success.
 */
esp_err_t mpu_driver_set_calibration(mpu_handle_t handle, const mpu_calibration_t *calib);

/**
 * @brief Perform a simple automatic calibration (recommended at startup).
 *
 * Assumes the sensor is stationary and level. Collects samples and computes
 * average offsets for gyroscope and accelerometer.
 *
 * @param handle MPU instance handle.
 * @param duration_ms Duration of calibration in milliseconds.
 * @return ESP_OK on success.
 */
esp_err_t mpu_driver_calibrate(mpu_handle_t handle, uint32_t duration_ms);

/**
 * @brief Delete an MPU instance and free resources.
 *
 * @param handle MPU instance handle.
 * @return ESP_OK on success.
 */
esp_err_t mpu_driver_delete(mpu_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* MPU_DRIVER_H */