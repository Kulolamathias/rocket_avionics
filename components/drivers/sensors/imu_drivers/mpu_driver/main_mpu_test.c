
/**
 * @file main.c
 * @brief Standalone MPU driver test – prints raw data and orientation.
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This test verifies the MPU driver functionality without any core or service
 * layers. It initialises I2C, creates the MPU driver, performs calibration,
 * and continuously reads and prints raw sensor data and orientation.
 *
 * =============================================================================
 * HARDWARE REQUIREMENTS
 * =============================================================================
 * - MPU-9250/6500/9255 connected to I2C pins (SDA=GPIO21, SCL=GPIO22).
 * - Power (3.3V) and GND connected.
 * =============================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "mpu_driver.h"
#include <math.h>
#include <inttypes.h>

static const char *TAG = "MPU_TEST";

/* I2C configuration */
#define I2C_MASTER_PORT     I2C_NUM_0
#define I2C_MASTER_SDA      8
#define I2C_MASTER_SCL      9
#define I2C_MASTER_FREQ_HZ  100000

/* MPU configuration */
#define MPU_SAMPLE_RATE_HZ  200      /* 200 Hz (5 ms between reads) */
#define ENABLE_MAGNETOMETER true     /* Set false for MPU-6500 */

/* Helper: initialise I2C */
static void init_i2c(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA,
        .scl_io_num = I2C_MASTER_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_PORT, conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "I2C master initialised");
}

/* Helper: print raw data */
static void print_raw(const mpu_raw_data_t *raw)
{
    ESP_LOGI(TAG, "Raw: accel=(%.3f, %.3f, %.3f) m/s², gyro=(%.3f, %.3f, %.3f) °/s",
             raw->accel_x, raw->accel_y, raw->accel_z,
             raw->gyro_x, raw->gyro_y, raw->gyro_z);
    if (raw->mag_valid) {
        ESP_LOGI(TAG, "     mag=(%.3f, %.3f, %.3f) µT", raw->mag_x, raw->mag_y, raw->mag_z);
    }
}

/* Helper: print orientation */
static void print_orientation(const mpu_orientation_t *orient)
{
    ESP_LOGI(TAG, "Orientation: roll=%.3f°, pitch=%.3f°, yaw=%.3f°",
             orient->roll_rad * 180.0f / M_PI,
             orient->pitch_rad * 180.0f / M_PI,
             orient->yaw_rad * 180.0f / M_PI);
    ESP_LOGI(TAG, "Quaternion: w=%.4f, x=%.4f, y=%.4f, z=%.4f",
             orient->q_w, orient->q_x, orient->q_y, orient->q_z);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting MPU driver test");

    /* Initialise I2C */
    init_i2c();

    /* Create MPU driver */
    mpu_config_t mpu_cfg = {
        .i2c_port = I2C_MASTER_PORT,
        .i2c_addr = 0x68,                     /* default address */
        .scl_pin = I2C_MASTER_SCL,
        .sda_pin = I2C_MASTER_SDA,
        .sample_rate_hz = MPU_SAMPLE_RATE_HZ,
        .enable_magnetometer = ENABLE_MAGNETOMETER,
        .calibration = {0},                   /* zero offsets initially */
    };

    mpu_handle_t mpu;
    esp_err_t ret = mpu_driver_create(&mpu_cfg, &mpu);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MPU driver: %d", ret);
        return;
    }

    /* Optional: automatic calibration (sensor must be stationary) */
    ESP_LOGI(TAG, "Starting auto‑calibration (keep sensor still)");
    ret = mpu_driver_calibrate(mpu, 5000);    /* 5 seconds */
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Calibration failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "Calibration done");
    }

    /* Main loop: read and print at the configured rate */
    uint32_t interval_ms = 1000 / MPU_SAMPLE_RATE_HZ;
    if (interval_ms < 1) interval_ms = 1;

    while (1) {
        ret = mpu_driver_update(mpu);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Update failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        mpu_raw_data_t raw;
        mpu_driver_get_raw(mpu, &raw);
        print_raw(&raw);

        mpu_orientation_t orient;
        mpu_driver_get_orientation(mpu, &orient);
        print_orientation(&orient);

        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}
