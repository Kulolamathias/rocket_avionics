#if 0



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
 * @brief Test HX711 load cell with continuous sampling.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "loadcell_driver.h"

static const char *TAG = "LOADCELL_TEST";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting HX711 load cell test");

    /* Configuration */
    loadcell_config_t cfg = {
        .sample_rate_hz = 80,
        .filter_window_size = 10,
        .calibration = { .offset_raw = 0, .scale_newtons_per_count = 1.0f },
        .hw = { .sck_pin = 5, .dout_pin = 4 }
    };

    loadcell_handle_t loadcell;
    esp_err_t ret = loadcell_driver_create(&cfg, &loadcell);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Create failed: %d", ret);
        return;
    }

    /* Zero calibration */
    ESP_LOGI(TAG, "Zero calibration...");
    ret = loadcell_driver_calibrate(loadcell, 0.0f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Zero calibration failed: %d", ret);
        loadcell_driver_delete(loadcell);
        return;
    }

    /* Scale calibration */
    ESP_LOGI(TAG, "Place known weight (228g = 2.2367 N) and wait 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ret = loadcell_driver_calibrate(loadcell, 2.2367f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scale calibration failed: %d", ret);
        loadcell_driver_delete(loadcell);
        return;
    }

    /* Start continuous sampling */
    ret = loadcell_driver_start_sampling(loadcell);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sampling: %d", ret);
        loadcell_driver_delete(loadcell);
        return;
    }

    /* Allow first sample to be taken */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Main loop – read and print at a comfortable rate */
    while (1) {
        float force;
        ret = loadcell_driver_get_latest(loadcell, &force);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Force: %.3f N", force);
        } else {
            ESP_LOGD(TAG, "No new data yet");
        }
        vTaskDelay(pdMS_TO_TICKS(10));  /* Print 10 times per second */
    }
}


























#endif