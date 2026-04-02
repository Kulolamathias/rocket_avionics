#if 1



/**
 * @file main.c
 * @brief Test entry – runs all selected tests.
 *
 * =============================================================================
 * ARCHITECTURAL ROLE
 * =============================================================================
 * This main file initialises the common infrastructure (core, services,
 * network stack, NVS) once, then runs the specified test suite.
 * New tests can be added by including their headers and calling their run
 * functions in the test suite list.
 *
 * =============================================================================
 * TEST SUITE SELECTION
 * =============================================================================
 * Currently, only timer_service_test is enabled because core_self_test would
 * conflict with real services (it registers its own mock handlers). To run
 * core_self_test, it must be built in a separate configuration without services.
 *
 * =============================================================================
 * @author matthithyahu
 * @date 2026
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "command_router.h"
#include "event_dispatcher.h"
#include "service_manager.h"

/* Test headers */
// #include "core_self_test.h"   // Disabled – conflicts with real services
// #include "timer_service_test.h"
#include "wifi_mqtt_test.h"
#include "ultrasonic_test.h"


static const char *TAG = "MAIN";

/**
 * @brief List of test run functions.
 * Add new tests here.
 */
static struct {
    const char *name;
    esp_err_t (*run)(void);
} s_tests[] = {
    // { "timer_service", timer_service_test_run },
    // { "wifi_mqtt", wifi_mqtt_test_run },
    // { "ultrasonic", ultrasonic_test_run },
    // { "core_self", core_self_test_run },
};

#define TEST_COUNT (sizeof(s_tests) / sizeof(s_tests[0]))

void app_main(void)
{
    esp_err_t ret;

    /* --------------------------------------------------------------------
     * 1. Core initialisation
     * -------------------------------------------------------------------- */
    command_router_init();
    ret = event_dispatcher_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_init failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------------------
     * 2. NVS (required for WiFi, etc.)
     * -------------------------------------------------------------------- */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --------------------------------------------------------------------
     * 3. TCP/IP stack and default event loop
     * -------------------------------------------------------------------- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* --------------------------------------------------------------------
     * 4. Service manager lifecycle
     * -------------------------------------------------------------------- */
    ret = service_manager_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_init_all failed: %d", ret);
        return;
    }

    ret = service_manager_register_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_register_all failed: %d", ret);
        return;
    }

    /* Lock the command router – after this no more handlers can be registered */
    command_router_lock();

    ret = service_manager_start_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "service_manager_start_all failed: %d", ret);
        return;
    }

    /* --------------------------------------------------------------------
     * 5. Start the event dispatcher (now that everything is ready)
     * -------------------------------------------------------------------- */
    ret = event_dispatcher_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_start failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "System ready. Running tests...");

    /* --------------------------------------------------------------------
     * 6. Run all tests sequentially
     * -------------------------------------------------------------------- */
    for (size_t i = 0; i < TEST_COUNT; i++) {
        ESP_LOGI(TAG, "=== Running test: %s ===", s_tests[i].name);
        ret = s_tests[i].run();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Test %s failed: %d", s_tests[i].name, ret);
        } else {
            ESP_LOGI(TAG, "Test %s passed.", s_tests[i].name);
        }
        /* Optional: small delay between tests */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "All tests completed. System idling.");

    /* Keep the system alive */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

















#else 












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



























#endif