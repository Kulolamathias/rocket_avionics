#if 1




#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "loadcell_driver.h"

static const char *TAG = "LOADCELL_TEST";

// Target sample rate (Hz) – the driver will read as fast as possible, but we try to maintain this rate
#define TARGET_RATE_HZ 80
// Log interval (seconds)
#define LOG_INTERVAL_SEC 1

void app_main(void)
{
    loadcell_config_t cfg = {
        .sample_rate_hz = TARGET_RATE_HZ,
        .filter_window_size = 10,
        .calibration = { .offset_raw = 0, .scale_newtons_per_count = 1.0f },
        .hw = { .sck_pin = 5, .dout_pin = 4 }
    };

    loadcell_handle_t loadcell;
    ESP_ERROR_CHECK(loadcell_driver_create(&cfg, &loadcell));

    ESP_LOGI(TAG, "Zero calibration (remove weight)");
    ESP_ERROR_CHECK(loadcell_driver_calibrate(loadcell, 0.0f));

    ESP_LOGI(TAG, "Place 228g weight (2.2367 N) and wait 5 seconds");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_ERROR_CHECK(loadcell_driver_calibrate(loadcell, 2.2367f));

    ESP_LOGI(TAG, "Reading at max speed, logging every %d s", LOG_INTERVAL_SEC);

    int64_t last_log = esp_timer_get_time();
    uint32_t sample_count = 0;
    float last_force = 0;

    // Timing control for target rate
    uint32_t interval_us = 1000000 / TARGET_RATE_HZ;
    int64_t next_read = esp_timer_get_time();

    while (1) {
        float force;
        if (loadcell_driver_read_filtered(loadcell, &force) == ESP_OK) {
            last_force = force;
            sample_count++;
        }

        int64_t now = esp_timer_get_time();
        if (now - last_log >= LOG_INTERVAL_SEC * 1000000) {
            float actual_rate = (float)sample_count * 1000000.0f / (now - last_log);
            ESP_LOGI(TAG, "Actual rate: %.1f Hz, Force: %.3f N", actual_rate, last_force);
            sample_count = 0;
            last_log = now;
        }

        // Maintain target rate (if possible)
        next_read += interval_us;
        now = esp_timer_get_time();
        if (next_read > now) {
            vTaskDelay(pdMS_TO_TICKS((next_read - now) / 1000));
        } else {
            // Drift: reset to now + interval
            next_read = now + interval_us;
        }
    }
}








#else


/**
 * @file main.c
 * @brief Standalone load cell driver test – measures thrust at high frequency.
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 * This test verifies the load cell driver's ability to sample continuously
 * at a high rate (e.g., 500 Hz) with deterministic timing and moving average
 * filtering. It prints the filtered force every 100 ms and also measures the
 * actual sampling rate.
 *
 * =============================================================================
 * HARDWARE REQUIREMENTS
 * =============================================================================
 * - A load cell connected to an ADC (SPI, I2C, or internal). You must adapt
 *   the `read_adc_raw()` function in `loadcell_driver.c` to your hardware.
 * - For testing without hardware, you can use the internal ADC with a
 *   potentiometer or leave it unconnected (will read noise).
 * =============================================================================
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "loadcell_driver.h"

static const char *TAG = "LOADCELL_TEST";

/* Configuration – change according to your hardware */
#define LOADCELL_SAMPLE_RATE_HZ  500      /* Desired sampling rate (Hz) */
#define LOADCELL_FILTER_WINDOW   20       /* Moving average window size */
#define USE_INTERNAL_ADC         1        /* Set to 1 for internal ADC, 0 for external */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting load cell driver test (target %d Hz)", LOADCELL_SAMPLE_RATE_HZ);

    /* Configure the load cell */
    loadcell_config_t cfg = {0};

#if USE_INTERNAL_ADC
    cfg.adc_type = LOADCELL_ADC_INTERNAL;
    cfg.hw.internal.adc_channel = ADC1_CHANNEL_0;   /* GPIO36 */
    cfg.hw.internal.attenuation = ADC_ATTEN_DB_11;  /* 0‑3.6V range */
#else
    /* Example for SPI ADC – adapt to your device */
    cfg.adc_type = LOADCELL_ADC_SPI;
    cfg.hw.spi.spi_host = SPI2_HOST;
    cfg.hw.spi.cs_pin = 5;
    cfg.hw.spi.sck_pin = 18;
    cfg.hw.spi.miso_pin = 19;
    cfg.hw.spi.mosi_pin = 23;
    cfg.hw.spi.clock_speed_hz = 1000000;
#endif

    cfg.sample_rate_hz = LOADCELL_SAMPLE_RATE_HZ;
    cfg.filter_window_size = LOADCELL_FILTER_WINDOW;
    /* Initial calibration (will be overwritten later if you calibrate) */
    cfg.calibration.offset_newtons = 0;
    cfg.calibration.scale_newtons_per_count = 1.0f;   /* temporary */

    loadcell_handle_t loadcell;
    esp_err_t ret = loadcell_driver_create(&cfg, &loadcell);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create load cell driver: %d", ret);
        return;
    }

    /* Optional: perform calibration with a known weight (e.g., 9.81 N = 1 kg) */
    ESP_LOGI(TAG, "Place a known weight (e.g., 1 kg) on the load cell and press Enter...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Calibrating with 9.81 N...");
    ret = loadcell_driver_calibrate(loadcell, 9.81f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Calibration failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "Calibration done. You can now remove the weight.");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Start continuous sampling */
    ret = loadcell_driver_start(loadcell);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sampling: %d", ret);
        loadcell_driver_delete(loadcell);
        return;
    }

    /* Measure actual sampling rate and print data */
    uint32_t sample_count = 0;
    int64_t last_time = esp_timer_get_time();
    float latest_force = 0;

    while (1) {
        /* Get the latest filtered value (non‑blocking) */
        ret = loadcell_driver_get_latest(loadcell, &latest_force);
        if (ret == ESP_OK) {
            sample_count++;
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Get latest failed: %d", ret);
        }

        /* Every 1 second, print statistics */
        int64_t now = esp_timer_get_time();
        if (now - last_time >= 1000000) {
            float actual_rate = (float)sample_count * 1000000.0f / (now - last_time);
            ESP_LOGI(TAG, "Rate: %.1f Hz (target %d Hz), Force: %.3f N",
                     actual_rate, LOADCELL_SAMPLE_RATE_HZ, latest_force);
            sample_count = 0;
            last_time = now;
        }

        /* Small delay to allow other tasks (not critical for sampling) */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}






#endif