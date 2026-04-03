#if 1



/**
 * @file loadcell_driver.h
 * @brief Load Cell Driver – HX711, scalable for other ADCs.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to a load cell connected
 * to an HX711 amplifier. It handles sampling, moving average filtering,
 * calibration, and continuous sampling via a dedicated FreeRTOS task.
 *
 * It contains NO business logic, NO command handling, and NO event posting.
 * =============================================================================
 */

#ifndef LOADCELL_DRIVER_H
#define LOADCELL_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle for a load cell instance. */
typedef struct loadcell_handle_t *loadcell_handle_t;

/** Configuration for HX711. */
typedef struct {
    gpio_num_t sck_pin;      /**< SCK (clock) pin */
    gpio_num_t dout_pin;     /**< DOUT (data) pin */
} loadcell_hx711_config_t;

/** Calibration data. */
typedef struct {
    int32_t offset_raw;                 /**< Raw ADC count at zero load */
    float scale_newtons_per_count;      /**< Conversion factor from ADC counts to newtons */
} loadcell_calibration_t;

/** Main configuration for a load cell. */
typedef struct {
    uint32_t sample_rate_hz;        /**< Desired sampling rate (Hz) – max 80 for HX711 */
    uint32_t filter_window_size;    /**< Moving average window size (1 = no filtering) */
    loadcell_calibration_t calibration; /**< Initial calibration (zero if unknown) */
    loadcell_hx711_config_t hw;     /**< Hardware configuration */
} loadcell_config_t;

/**
 * @brief Create a load cell instance.
 *
 * @param cfg   Configuration structure.
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t loadcell_driver_create(const loadcell_config_t *cfg, loadcell_handle_t *out_handle);

/**
 * @brief Perform a single measurement (blocking, for calibration or one‑off).
 *
 * @param handle Load cell instance.
 * @param[out] newtons Force in newtons (calibrated using current calibration).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t loadcell_driver_measure_once(loadcell_handle_t handle, float *newtons);

/**
 * @brief Start continuous sampling (non‑blocking).
 *
 * Creates a task that samples at the configured rate and applies filtering.
 * The latest filtered value is placed in a queue (size 1, overwritten).
 *
 * @param handle Load cell instance.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t loadcell_driver_start_sampling(loadcell_handle_t handle);

/**
 * @brief Stop continuous sampling.
 *
 * @param handle Load cell instance.
 * @return ESP_OK on success.
 */
esp_err_t loadcell_driver_stop_sampling(loadcell_handle_t handle);

/**
 * @brief Get the latest filtered force value (non‑blocking).
 *
 * @param handle Load cell instance.
 * @param[out] newtons Latest force in newtons.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no new data available.
 */
esp_err_t loadcell_driver_get_latest(loadcell_handle_t handle, float *newtons);

/**
 * @brief Update calibration.
 *
 * Call with known_newtons = 0 to set zero offset.
 * Then apply a known weight and call with known_newtons > 0 to set scale.
 *
 * @param handle Load cell instance.
 * @param known_newtons Known force applied (0 for zero calibration).
 * @return ESP_OK on success.
 */
esp_err_t loadcell_driver_calibrate(loadcell_handle_t handle, float known_newtons);

/**
 * @brief Delete a load cell instance and free resources.
 *
 * @param handle Load cell instance.
 * @return ESP_OK on success.
 */
esp_err_t loadcell_driver_delete(loadcell_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* LOADCELL_DRIVER_H */

































#else




/**
 * @file loadcell_driver.h
 * @brief Load Cell Driver – supports HX711, SPI, I2C, internal ADC.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This driver provides a handle‑based interface to a load cell connected
 * to an ADC (HX711, SPI, I2C, or internal). It handles sampling, filtering,
 * calibration, and deterministic data delivery.
 *
 * It contains NO business logic, NO command handling, and NO event posting.
 * =============================================================================
 */

#ifndef LOADCELL_DRIVER_H
#define LOADCELL_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle for a load cell instance. */
typedef struct loadcell_handle_t *loadcell_handle_t;

/** ADC type selection – only one enumeration. */
typedef enum {
    LOADCELL_ADC_HX711,      /**< HX711 load cell amplifier (digital) */
    LOADCELL_ADC_SPI,        /**< External SPI ADC (e.g., ADS1263, MCP3564) */
    LOADCELL_ADC_I2C,        /**< External I2C ADC (e.g., ADS1115) */
    LOADCELL_ADC_INTERNAL,   /**< ESP32 internal ADC (12‑bit, noisy) */
} loadcell_adc_type_t;

/** Configuration for HX711. */
typedef struct {
    gpio_num_t sck_pin;      /**< SCK (clock) pin */
    gpio_num_t dout_pin;     /**< DOUT (data) pin */
} loadcell_hx711_config_t;

/** Configuration for SPI ADC (placeholder – extend as needed). */
typedef struct {
    spi_host_device_t spi_host;     /**< SPI host (SPI2_HOST, SPI3_HOST) */
    int cs_pin;                     /**< Chip select pin */
    int sck_pin;                    /**< Clock pin */
    int miso_pin;                   /**< MISO pin (data from ADC) */
    int mosi_pin;                   /**< MOSI pin (unused for many ADCs) */
    uint32_t clock_speed_hz;        /**< SPI clock frequency */
} loadcell_spi_config_t;

/** Configuration for I2C ADC (placeholder – extend as needed). */
typedef struct {
    i2c_port_t i2c_port;            /**< I2C port (I2C_NUM_0, I2C_NUM_1) */
    uint8_t i2c_addr;               /**< I2C device address */
} loadcell_i2c_config_t;

/** Configuration for internal ADC. */
typedef struct {
    adc_oneshot_unit_handle_t adc_handle;  /**< ADC oneshot handle (created externally) */
    adc_channel_t channel;                /**< ADC channel (e.g., ADC_CHANNEL_0) */
    adc_atten_t attenuation;              /**< Attenuation (ADC_ATTEN_DB_11 for 0‑3.6V) */
} loadcell_internal_config_t;

/** Calibration data. */
typedef struct {
    int32_t offset_raw;                 /**< Raw ADC count at zero load */
    float scale_newtons_per_count;      /**< Conversion factor from ADC counts to newtons */
} loadcell_calibration_t;

/** Main configuration for a load cell. */
typedef struct {
    loadcell_adc_type_t adc_type;   /**< Type of ADC used */
    uint32_t sample_rate_hz;        /**< Desired sampling rate (Hz) – max depends on ADC */
    uint32_t filter_window_size;    /**< Moving average window size (1 = no filtering) */
    loadcell_calibration_t calibration; /**< Initial calibration (zero if unknown) */
    union {
        loadcell_hx711_config_t hx711;
        loadcell_spi_config_t spi;
        loadcell_i2c_config_t i2c;
        loadcell_internal_config_t internal;
    } hw;                           /**< Hardware‑specific configuration */
} loadcell_config_t;

/**
 * @brief Create a load cell instance.
 *
 * @param cfg   Configuration structure.
 * @param out_handle Pointer to store the created handle.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t loadcell_driver_create(const loadcell_config_t *cfg, loadcell_handle_t *out_handle);

/**
 * @brief Perform a single measurement (blocking, for calibration).
 *
 * @param handle Load cell instance.
 * @param[out] newtons Force in newtons (calibrated using current calibration).
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t loadcell_driver_measure_once(loadcell_handle_t handle, float *newtons);

/**
 * @brief Start continuous sampling (non‑blocking).
 *
 * @param handle Load cell instance.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t loadcell_driver_start(loadcell_handle_t handle);

/**
 * @brief Stop continuous sampling.
 *
 * @param handle Load cell instance.
 * @return ESP_OK on success.
 */
esp_err_t loadcell_driver_stop(loadcell_handle_t handle);

/**
 * @brief Get the latest filtered force value (non‑blocking).
 *
 * @param handle Load cell instance.
 * @param[out] newtons Latest force in newtons.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no new data available.
 */
esp_err_t loadcell_driver_get_latest(loadcell_handle_t handle, float *newtons);

/**
 * @brief Update calibration.
 *
 * Call with known_newtons = 0 to set zero offset.
 * Then apply a known weight and call with known_newtons > 0 to set scale.
 *
 * @param handle Load cell instance.
 * @param known_newtons Known force applied (0 for zero calibration).
 * @return ESP_OK on success.
 */
esp_err_t loadcell_driver_calibrate(loadcell_handle_t handle, float known_newtons);

/**
 * @brief Delete a load cell instance and free resources.
 *
 * @param handle Load cell instance.
 * @return ESP_OK on success.
 */
esp_err_t loadcell_driver_delete(loadcell_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* LOADCELL_DRIVER_H */


#endif