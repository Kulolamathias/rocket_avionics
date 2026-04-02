/**
 * @file tests/src/ultrasonic_test.c
 * @brief Ultrasonic sensor test – verifies periodic readings.
 */

#include "ultrasonic_test.h"
#include "command_router.h"
#include "command_params.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UltrasonicTest";

esp_err_t ultrasonic_test_run(void)
{
    ESP_LOGI(TAG, "Starting ultrasonic test");

    /* Start periodic timer to trigger ultrasonic readings every 2 seconds */
    command_param_union_t params;
    cmd_start_timer_params_t timer_params = { .timeout_ms = 2000 };
    params.timer = timer_params;

    esp_err_t ret = command_router_execute(COMMAND_START_PERIODIC_TIMER, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start periodic timer: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Periodic timer started (2s interval). Expect ultrasonic readings.");

    /* Let it run for 20 seconds */
    vTaskDelay(pdMS_TO_TICKS(20000));

    ret = command_router_execute(COMMAND_STOP_PERIODIC_TIMER, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop periodic timer: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "Periodic timer stopped.");

    ESP_LOGI(TAG, "Ultrasonic test completed");
    return ESP_OK;
}